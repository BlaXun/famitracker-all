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
		"chord. Turn it off (000) before a note that shouldn't arpeggiate.",
		"Turns one note into a quick chord: the channel jumps between the note and the notes x and y semitones above "
		"it, so fast that the ear hears a chord. 047 is a major chord, 037 a minor one and 0C0 the note with its "
		"octave. 000 turns it off.", 0x47 });
	All.push_back({ "1xx", "slide up",
		"Slides the pitch up, xx pitch units every tick, without end.", "every channel with a pitch", "until 100, or another slide",
		"risers, sirens and sound effects; 101-104 climb slowly. With a halt after it, a note that flies off.",
		"Slides the pitch up and keeps going, like a siren or a rocket taking off; the bigger the number, the faster. "
		"100 stops it.", 0x03 });
	All.push_back({ "2xx", "slide down",
		"Slides the pitch down, xx pitch units every tick, without end.", "every channel with a pitch", "until 200, or another slide",
		"falls, bombs dropping, a dive at the end of a phrase (with a halt a row or two later).",
		"Slides the pitch down and keeps going, like a bomb falling; the bigger the number, the faster. 200 stops it.", 0x03 });
	All.push_back({ "3xx", "portamento",
		"Each new note glides from the pitch before to its own, xx pitch units a tick.", "every channel with a pitch",
		"until 300: every note glides while it's on",
		"legato leads and bass glides: 304-310 smooth, 320 and up quick. Turn it off (300) before notes that should jump.",
		"Makes each new note glide from the one before instead of jumping, like a singer sliding between notes; the "
		"number is how fast. It stays on for every note until 300 turns it off.", 0x08 });
	All.push_back({ "4xy", "vibrato",
		"Wobbles the pitch: x is how fast, y how deep (1-F each).", "every channel with a pitch", "until 400",
		"443-454 a singing lead, 4A2 a fast shimmer, 486 a wide wobble. Bring it in a beat after a long note starts, not on "
		"every short note (see the technique \"delayed vibrato\").",
		"Makes the note wobble up and down in pitch, like a singer holding a long note: x is how fast, y how deep. A "
		"gentle one brought in after a long note starts sounds the most natural. 400 turns it off.", 0x45 });
	All.push_back({ "7xy", "tremolo",
		"Wobbles the volume: x is how fast, y how deep.", "channels with a volume (not the triangle or the DPCM)", "until 700",
		"744 a gentle shimmer on pads and organs, 7A8 a fast throb.",
		"Makes the volume wobble, a shimmer or a throb, like waving a hand in front of a speaker: x is how fast, y how "
		"deep. 700 turns it off. The triangle can't, since it has no volume.", 0x64 });
	All.push_back({ "8xx", "pan",
		"Sets where the channel sounds from, between the speakers: 00 hard left, 80 the middle (where every channel starts), FF "
		"hard right, and the places between (40 halfway left, C0 halfway right; the far side fades, the near one keeps all of "
		"it). This FamiTracker's own stereo: its playback, File > Create WAV and the OGG files of export_for_game have it; "
		"FamiTracker 0.4.6 and NSF files play mono and leave it out.",
		"every channel", "until the next 8xx, over notes, releases and halts",
		"width: an echo on the other side of its lead (add_echo with side), chord voices apart (840 and 8C0), arpeggios or "
		"hi-hats alternating left and right (set_pan). Keep the bass, the kick, the snare and the lead in the middle (880): they "
		"carry the song, and many players hear it from one speaker.",
		"Places the channel between the left and right speakers: 00 hard left, 80 the middle, FF hard right. It stays "
		"until the next 8xx. It's this FamiTracker's own stereo, for its playback and the game's sound files; the NES "
		"and NSF files are mono.", 0x20 });
	All.push_back({ "Axy", "volume slide",
		"Slides the volume: A0y down, Ax0 up, by y or x eighths of a step a tick (one of them, the other 0).",
		"channels with a volume (not the triangle or the DPCM)", "until A00",
		"A01-A04 fade a held note out (give the next note its volume, since the slide leaves the channel quieter), A10-A40 "
		"swell one in (start it soft, at volume 3 or so); a fade at the end of a song.",
		"Fades the volume while the note plays: A0y down, Ax0 up, the number how fast. It fades a long note out or "
		"swells one in. A00 stops it.", 0x02 });
	All.push_back({ "Gxx", "note delay",
		"Holds this row's note back xx ticks, less than the speed (the ticks a row lasts).", "every channel", "this row only",
		"a swing for one part (G02 or G03 on its off-beats at speed 6; set_swing swings the whole song), flams and rolls on "
		"drums, an echo that lags, a less mechanical feel.",
		"Starts this row's note a few ticks late (a tick is a sixtieth of a second): a laid-back feel, a swing on some "
		"notes, or a quick double hit on a drum. It acts on its own row only.", 0x03 });
	All.push_back({ "Hxy", "hardware sweep up",
		"Starts the 2A03 pulse's own sweep: the pitch rises by itself, x how often (0 fastest to 7 slowest), y how much each "
		"time (1 the most to 7 the least).", "the 2A03 pulses only", "until the next note",
		"a rising zap or \"boing\", laser shots from a high note, quick glissandi. A sweep that runs off the top falls silent.",
		"Starts the 2A03 pulse's own sweep, which bends the pitch up all by itself: a rising zap or a boing. x is how "
		"often it moves (0 the fastest), y how far each time (1 the most). The next note ends it.", 0x71 });
	All.push_back({ "Ixy", "hardware sweep down",
		"Starts the 2A03 pulse's own sweep downward: the pitch falls by itself, x how often (0 fastest), y how much (1 the most).",
		"the 2A03 pulses only", "until the next note",
		"I12 a quick drop: a kick drum on a pulse (a low note, halted a row or two later); I71 a slow fall; dives.",
		"Starts the 2A03 pulse's own sweep downward: the pitch falls by itself, for laser shots, dives and a punchy "
		"kick drum on a pulse. x is how often it moves (0 the fastest), y how far each time (1 the most).", 0x12 });
	All.push_back({ "Pxx", "fine pitch",
		"Tunes the channel a little: P80 is in tune, P81-P8F a little higher, P7F-P70 a little lower (a period step each).",
		"every channel with a pitch", "until changed",
		"a chorus: double a part on two channels with P81 or P82 on one; fine tuning one chip against another.",
		"Tunes the channel a tiny bit higher or lower: P80 is in tune, above it a little higher, below it a little "
		"lower. A part doubled on two channels with P81 or P82 on one of them shimmers like a chorus.", 0x84 });
	All.push_back({ "Qxy", "note slide up",
		"Slides from the note up y semitones (1-F), at speed x (0-F, faster as it rises), and stays there.",
		"every channel with a pitch", "this row only",
		"bends and scoops: to land on a note, write the one y semitones below it with Qxy. Q31 a quick lift of a semitone, Q72 a "
		"fast whole-step bend, Q1C a slow rise of an octave.",
		"Slides the note up y semitones and stays there, quickly or slowly (x is the speed): a bend or a scoop into a "
		"note, like a guitarist bending a string. It acts on its own row only.", 0x72 });
	All.push_back({ "Rxy", "note slide down",
		"Slides from the note down y semitones (1-F), at speed x, and stays there.", "every channel with a pitch", "this row only",
		"falls at the end of a phrase (R5C, an octave down quickly), sighs (R22), sound effects.",
		"Slides the note down y semitones and stays there (x is the speed): a fall at the end of a phrase, a sigh, a "
		"dive. It acts on its own row only.", 0x5C });
	All.push_back({ "Sxx", "note cut",
		"Cuts the note off after xx ticks.", "every channel", "this row only",
		"staccato: S02-S04 make short, plucky notes; gated chords with 0xy; short drum hits on melodic channels.",
		"Stops the note after xx ticks, so it's short and bouncy: staccato notes, plucked sounds, short drum hits. It "
		"acts on its own row only.", 0x03 });
	All.push_back({ "Vxx", "timbre",
		"Changes the tone: a pulse's duty (2A03 and MMC5 pulses 00 12.5% thin, 01 25%, 02 50% round, 03 75%; VRC6 pulses "
		"00-07, 1/16 to 8/16), the noise's mode (00 hiss, 01 metallic), an N163 channel's wave (00-0F), what a 5B channel "
		"plays (01 its square, 02 the noise, 04 the envelope, added up: 03 square and noise).",
		"pulses, the noise, the N163 and the 5B channels", "until changed; an instrument's duty sequence takes over while it runs",
		"a brighter or thinner lead for a section, duties alternating for a \"talking\" lead, metallic noise for hats and zaps.",
		"Changes the channel's tone: on a pulse its width (00 thin and nasal, 01 bright, 02 round and hollow), on the "
		"noise its mode (00 hiss, 01 metallic), on the N163 its wave, on the 5B what it plays. It lasts until changed, "
		"though an instrument with its own duty sequence takes over while that runs.", 0x02 });
	All.push_back({ "Wxx", "DPCM pitch",
		"Plays the sample at rate xx (00-0F) instead of the instrument's.", "the DPCM channel", "this note",
		"the same drum sample higher or lower: toms from one sample, a tighter snare.",
		"Plays the drum sample faster and higher, or slower and lower (00 to 0F): toms from one sample, a tighter "
		"snare. Only the DPCM channel has it.", -1 });
	All.push_back({ "Xxx", "DPCM retrigger",
		"Plays the sample again every xx+1 ticks.", "the DPCM channel", "until the next note", "drum rolls and stutters (X03-X06).",
		"Plays the sample again and again, every few ticks: drum rolls and stutters. Only the DPCM channel has it.", -1 });
	All.push_back({ "Yxx", "DPCM sample offset",
		"Starts the sample xx times 64 bytes in.", "the DPCM channel", "this note",
		"a punchier kick from a longer sample; one long sample cut into slices.",
		"Starts the sample partway in (xx times 64 bytes), skipping its start: a punchier hit, or one long sample used "
		"as several. Only the DPCM channel has it.", -1 });
	All.push_back({ "Zxx", "DPCM delta counter",
		"Sets the DPCM channel's output level (00-7F) directly. The NES mixes the triangle, the noise and the DPCM together, so a "
		"high level makes the triangle and the noise quieter.", "the DPCM channel", "until changed",
		"Z00 at the song's start keeps the triangle and the noise at full volume; a jump in level pops, which some games use as a "
		"kick.",
		"Sets the DPCM channel's output level directly (00 to 7F). On the NES a higher level also makes the triangle "
		"and the noise quieter, so a level set at the song's start evens out a soft mix. A sudden jump pops, like a "
		"kick.", -1 });
	All.push_back({ "Hxx", "FDS modulation depth",
		"How deep the FDS's frequency modulator wobbles the tone (00-3F).", "the FDS channel", "until changed",
		"bell and vibrato tones on the FDS, with Ixx and Jxx for the speed.",
		"How strongly the FDS's modulator bends its tone (00 to 3F): a little is a gentle wobble or a bell-like ring, a "
		"lot a warbling, metallic sound. Only the FDS channel has it.", -1 });
	All.push_back({ "Ixx", "FDS modulation speed, high",
		"The high part (00-0F) of the FDS modulator's speed.", "the FDS channel", "until changed", "with Jxx, how fast the FDS tone wobbles.",
		"With Jxx, how fast the FDS's modulator wobbles its tone: Ixx is the coarse part (00 to 0F). Only the FDS "
		"channel has it.", -1 });
	All.push_back({ "Jxx", "FDS modulation speed, low",
		"The low part (00-FF) of the FDS modulator's speed.", "the FDS channel", "until changed", "with Ixx, how fast the FDS tone wobbles.",
		"With Ixx, how fast the FDS's modulator wobbles its tone: Jxx is the fine part (00 to FF). Only the FDS channel "
		"has it.", -1 });
	All.push_back({ "Hxy", "5B envelope",
		"Starts the Sunsoft 5B's envelope, which a note plays when its instrument's noise / mode sequence (or V04-V07) turns it "
		"on: y is its shape (8 a falling saw over and over, C a rising one, A and E triangles, 0 one fall to silence, D one rise "
		"that holds); x makes its speed follow the note, C at the note's own pitch (a buzz), B an octave under it, D an octave "
		"over, or 0 for the speed Ixx and Jxx set. The envelope sets the level (the volume column only silences it, at 0), and "
		"the three 5B channels share the one envelope.", "the 5B channels", "until changed",
		"the buzz bass (HC8, the 5B's envelope until a song sets one), a hard sync-like lead with the square on too (V05), slow "
		"swells and tremolos (HxA or HxE with x 0 and a long period).",
		"Starts the 5B's envelope, a shape that sets its level over and over: falling or rising saws, triangles, or one "
		"fall to silence (y). With x at C it follows the note's own pitch, a buzz; with x at 0, Ixx and Jxx set its "
		"speed. The 5B's three channels share it, on notes whose instrument (or V04) turns it on.", 0xC8 });
	All.push_back({ "Ixx", "5B envelope period, high",
		"The high byte (00-FF) of the 5B envelope's period, for Hxy with x 0: a saw shape repeats about 3500 / period times a "
		"second (I08 J00: 1.7 times), a triangle half as often.", "the 5B channels", "until changed",
		"with Jxx, a slow envelope for swells and tremolos.",
		"With Jxx, how slowly the 5B's envelope moves when it doesn't follow the note: Ixx is the coarse part, and "
		"bigger is slower. Only the 5B channels have it.", -1 });
	All.push_back({ "Jxx", "5B envelope period, low",
		"The low byte (00-FF) of the 5B envelope's period.", "the 5B channels", "until changed", "with Ixx, the envelope's speed.",
		"With Ixx, how slowly the 5B's envelope moves: Jxx is the fine part. Only the 5B channels have it.", -1 });
	return All;
}

std::vector<Technique> MakeTechniques()
{
	std::vector<Technique> All;
	All.push_back({ "echo", "delay, echo channel, reverb",
		"The lead's notes again on a free channel, a dotted eighth or a beat later, at about 40% of the lead's loudness (a lead "
		"at volume 6 echoes at 2 or 3), same instrument and octave: space without effects. Atmospheric tracks keep one under "
		"nearly every melodic line; a step of detune (P81 or P7F) on the echo makes it shimmer where the two overlap. add_echo "
		"makes one. With no channel free, repeat a long note softly on the rows after it." });
	All.push_back({ "chorus", "detune, pxx, double, unison, thick lead",
		"Two channels play the same part, one a little sharp: P81 or P82 on the second (P80 is in tune). The slow beat between them "
		"shimmers and thickens a lead; two pulses with different duties (or a 2A03 and a VRC6 pulse) even more." });
	All.push_back({ "delayed vibrato", "vibrato, singing lead",
		"Start long notes plain and bring the vibrato in after a beat: the note, then 443-454 a row or two later (4xy on that row), "
		"and 400 before the next note if it shouldn't wobble. add_vibrato does it with delay_beats, and design_instrument can build "
		"it into an instrument." });
	All.push_back({ "swing", "shuffle, groove, gxx, fxx",
		"Alternate the speed every half beat, longer then shorter, so the rows themselves swing: at speed 6, F08 and F04 in turn "
		"is a triplet feel and F07 and F05 a light one, averaging 6 so the tempo stays (F06 and F03 at speed 4.5, F04 and F02 at "
		"3). Every channel swings together, and notes written later too; set_swing does it. For one part, delay its off-beat "
		"notes with Gxx instead (G02 or G03 at speed 6), all of them, or it sounds like mistakes: set_swing by delays does it on "
		"the channels named, worked out from each row's speed (a shuffled hi-hat or arpeggio over a straight bass). The same "
		"trick gives speeds between whole numbers: F03 F03 F03 F04 is 3.25 ticks a row." });
	All.push_back({ "staccato", "short notes, sxx, gated, stabs",
		"Cut notes short with Sxx: S02-S04 make plucky, bouncing notes; an arpeggio chord (037) with S03 sounds gated. A halt (---) "
		"or release (===) a row after the note does the same on the row grid." });
	All.push_back({ "fades and swells", "fade, swell, crescendo, decrescendo, volume slide, axy",
		"A held note fades out with A0y (A01 slowly, A08 quickly) and swells in with Ax0 from a soft start (volume 3, A10-A40); "
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
		"A scoop into a note: write it a semitone or two below and slide up with Q21/Q32. A fall at a phrase's end: Rxy, a sigh "
		"of a semitone or two (R41, R42) as the note ends, or a long drop (R5C, an octave down). For every note gliding into the "
		"next, 3xx portamento (306 smooth), off again with 300. polish's touches scoops and falls write the short ones." });
	All.push_back({ "arpeggio chords", "arpeggio, chords on one channel, 0xy",
		"0xy plays a chord on one channel: 037 minor, 047 major, 0C0 octaves, 059 or 057 open. It frees the other channels for "
		"a melody and a bass; change the arpeggio with the chords, and 000 for notes that should stand alone." });
	All.push_back({ "lush chords", "sevenths, ninths, jazz chords, arpeggio instruments, chord instruments",
		"For chords of four or more notes, put the arpeggio in an instrument: an arpeggio sequence like 0 0 4 4 7 7 11 11, "
		"looping, plays a major seventh two frames a note (0 0 3 3 7 7 10 10 a minor seventh, 0 4 7 14 an add9 with its ninth on "
		"top), one instrument a chord shape, played on the root softly (volume 4-7) and struck again on a rhythm. Slower (three "
		"or four frames a note) is dreamier; descending or spread over two octaves sounds wider. add_chords_and_bass with "
		"chord_style lush makes them." });
	All.push_back({ "quiet mixing", "mixing, balance, volume, soft, warm, levels",
		"Pulses well under full volume: a lead at 4-8 rather than 15, echoes at 2-3, chords at 4-7, drums under 12, with the "
		"triangle (which has no volume) carrying the low end. The pulses stop burying the triangle and the noise, the sound gets "
		"warmer and the volume column has room for dynamics. A higher DPCM level (Zxx) then softens the triangle and the noise "
		"too, so nothing sticks out." });
	All.push_back({ "triangle kick", "kick on the triangle, triangle drums",
		"A kick on the triangle frees the noise for snares and hats: a short triangle note that drops fast, a C-3 or so with "
		"an instrument whose arpeggio or pitch falls an octave in a few ticks (or 2xx), cut after a row with S02 or a halt. A "
		"staple of Capcom and Konami tracks." });
	All.push_back({ "dpcm tricks", "dpcm, samples, zxx, yxx, wxx, xxx",
		"Z00 at the start keeps the triangle and the noise at full volume; a higher level quiets them, since the NES mixes the "
		"three together: Z20 to about 80%, Z3A 65%, Z60 half, Z7F 43%. Atmospheric tracks set Z30-Z7F at the start so a soft "
		"mix stays even. Yxx starts a sample later, Wxx plays it higher or lower, Xxx retriggers it for rolls. add_sample_drums "
		"adds a kit." });
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
	if (C.Chip == "5B")
		return 7;
	return -1;
}

// The chip of an entry for H, I or J: those of the FDS and the 5B are theirs, the others (the 2A03 pulses' sweep) ""
std::string EntryChip(const EffectInfo &E)
{
	const std::string Name = E.Name;
	if (Name.compare(0, 3, "FDS") == 0)
		return "FDS";
	if (Name.compare(0, 2, "5B") == 0)
		return "5B";
	return std::string();
}

// The chip whose H, I and J a channel plays: the FDS's modulator, the 5B's envelope, "" for the others (H and I the sweep)
std::string LetterChip(const ChannelInfo *pChannel)
{
	return pChannel != 0 && (pChannel->Chip == "FDS" || pChannel->Chip == "5B") ? pChannel->Chip : std::string();
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
			return "Axy slides one way at a time: A0y down, Ax0 up.";
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
			std::snprintf(Text, sizeof(Text), "Vxx changes the tone of pulses, the noise, the N163 and the 5B; %s has none to change.",
				ChannelName(C).c_str());
		else if (Param > Most)
			std::snprintf(Text, sizeof(Text), "%s takes V00-V%02X%s.", ChannelName(C).c_str(), Most,
				C.Kind == KIND_PULSE && Most == 3 ? " (12.5%, 25%, 50%, 75%)" : (C.Kind == KIND_NOISE ? " (hiss, metallic)" :
				(C.Chip == "5B" ? " (1 the square, 2 the noise, 4 the envelope, added up)" : "")));
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
		if (C.Chip != "FDS") {
			std::snprintf(Text, sizeof(Text), "Jxx (with Hxx and Ixx) is the FDS's modulator on the FDS channel and the envelope "
				"on the 5B's; %s plays neither.", ChannelName(C).c_str());
			break;
		}
		if (Effect == EFF_FDS_MOD_DEPTH && Param > 0x3F)
			return "Hxx (the FDS's modulation depth) goes from H00 to H3F.";
		if (Effect == EFF_FDS_MOD_SPEED_HI && Param > 0x0F)
			return "Ixx (the FDS's modulation speed, high) goes from I00 to I0F.";
		break;
	case EFF_SUNSOFT_ENV_TYPE: case EFF_SUNSOFT_ENV_HI: case EFF_SUNSOFT_ENV_LO:
		if (C.Chip != "5B")
			return "Hxy, Ixx and Jxx steer the Sunsoft 5B's envelope on its channels; elsewhere H and I are the 2A03 pulses' sweep "
				"and the FDS's modulator.";
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
	case EFF_PAN:
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
		return Written + " (a groove) comes from 0CC-FamiTracker, not 0.4.6. For a swing, alternate the speed every half beat "
			"with Fxx (set_swing does it), or hold the off-beat notes back with Gxx.";
	case 'T':
		return Written + " (a transpose xx ticks later) comes from 0CC-FamiTracker, not 0.4.6. Write the next note, or slide to it "
			"with Qxy or Rxy.";
	case '5':
		return Written + " (portamento and volume slide at once) is a MOD and XM effect. In FamiTracker put 3xx and Axy in two "
			"effect columns.";
	case '6':
		return Written + " (vibrato and volume slide at once) is a MOD and XM effect. In FamiTracker put 4xy and Axy in two effect "
			"columns.";
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
	"Tone: Vxx timbre (a pulse's duty, the noise's mode, an N163 wave, what a 5B channel plays).\n"
	"Samples (the DPCM channel): Wxx pitch, Xxx retrigger, Yxx sample offset, Zxx delta counter.\n"
	"The FDS: Hxx modulation depth, Ixx and Jxx modulation speed.\n"
	"The Sunsoft 5B: Hxy its envelope's shape and how it follows the note, Ixx and Jxx its period.\n"
	"Stereo (this FamiTracker's own, not 0.4.6's): 8xx pan, 00 left, 80 the middle, FF right; its playback and the OGG files "
	"for games have it, NSF files don't.\n"
	"The song (set_tempo and set_loop write these): Fxx speed or tempo, Bxx jump, Dxx skip, Cxx halt. Exx (volume) is only kept "
	"for old songs: use the volume column.\n"
	"Not in 0.4.6: Lxx, Mxy, Oxx, Txy and the like come from later FamiTrackers (0CC); 5xy, 6xy and 9xx from MOD and XM. "
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

const std::vector<EffectInfo> &SongEffectReference()
{
	static const std::vector<EffectInfo> All = {
		{ "Fxx", "speed or tempo",
			"Sets the song's speed (under the split point, 20 unless the module sets another: the ticks a row lasts) or its "
			"tempo (from there up), from this row on.", "any channel: it steers the whole song", "until the next Fxx",
			"a slower or faster part; a swing, speeds alternating every half beat.",
			"Changes how fast the whole song plays from this row on: a number under 20 sets the speed, how many ticks each row "
			"lasts (so a bigger one is slower), and 20 and up sets the tempo. It can go on any channel.", -1 },
		{ "Bxx", "jump",
			"After this row, the song goes on at frame xx.", "any channel: it steers the whole song", "this row",
			"the loop: on the last frame, B and the frame the loop starts at.",
			"Jumps to frame xx after this row, so the song goes on from there. On the song's last frame it makes the loop: the "
			"music comes back round to that frame and plays on.", -1 },
		{ "Dxx", "skip",
			"Ends this frame after this row; the next frame starts at its row xx.", "any channel: it steers the whole song",
			"this row", "a frame shorter than its patterns; starting the next one partway in.",
			"Ends this frame early: after this row the next frame starts, at its row xx (D00 at its top). It shortens a frame "
			"without changing its patterns.", -1 },
		{ "Cxx", "halt",
			"Stops the song after this row.", "any channel: it steers the whole song", "this row",
			"jingles and endings that don't loop.",
			"Stops the song at this row, for a jingle or an ending that doesn't loop. Without it, or a jump, a song starts over "
			"from its first frame when it ends.", -1 },
		{ "Exx", "old volume",
			"Sets the channel's volume; FamiTracker 0.4.6 keeps it only for old songs.", "channels with a volume", "until changed",
			"nothing new: give notes a volume in the volume column instead.",
			"An old way to set a channel's volume, kept so that old songs still play the same. Use the volume column instead.",
			-1 },
	};
	return All;
}

namespace {

// "a major chord" for the arpeggio's two steps above the note, "" when they make none with a name
std::string ArpeggioChord(int x, int y)
{
	struct Named { int x, y; const char *Name; };
	static const Named NAMES[] = {
		{ 4, 7, "a major chord" }, { 3, 7, "a minor chord" }, { 5, 7, "a sus4 chord" }, { 2, 7, "a sus2 chord" },
		{ 3, 6, "a diminished chord" }, { 4, 8, "an augmented chord" }, { 7, 12, "the fifth and the octave" },
		{ 4, 9, "a major sixth chord" }, { 3, 8, "a major chord with its third at the bottom" },
		{ 5, 9, "a major chord with its fifth at the bottom" }, { 12, 0, "octaves" }, { 7, 0, "a fifth, a power chord" },
		{ 12, 7, "the octave and the fifth" }, { 4, 0, "a major third" }, { 3, 0, "a minor third" },
	};
	for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); ++i)
		if (NAMES[i].x == x && NAMES[i].y == y)
			return NAMES[i].Name;
	return std::string();
}

std::string Number(int Value)
{
	char Text[16];
	std::snprintf(Text, sizeof(Text), "%d", Value);
	return Text;
}

std::string Hex(int Value)
{
	char Text[8];
	std::snprintf(Text, sizeof(Text), "%02X", Value & 0xFF);
	return Text;
}

// The 5B envelope's shapes (y of Hxy), as the YM2149 plays them
const char *EnvelopeShape(int y)
{
	if (y < 4 || y == 9)
		return "one fall to silence";
	if (y < 8 || y == 15)
		return "one rise, then silence";
	switch (y) {
	case 8: return "a falling saw, over and over";
	case 10: return "a triangle, down first, over and over";
	case 11: return "one fall, then held at the top";
	case 12: return "a rising saw, over and over";
	case 13: return "one rise, then held at the top";
	default: return "a triangle, up first, over and over";
	}
}

}	// namespace

std::string DescribeEffect(int Effect, int Param, const ChannelInfo &Channel, int SpeedSplit)
{
	const int x = (Param >> 4) & 15, y = Param & 15, P = Param & 0xFF;
	const bool Fds = Channel.Chip == "FDS", S5B = Channel.Chip == "5B";
	// H, I and J are the FDS's and the 5B's own on their channels (ParseChannelEffect)
	if (Fds && (Effect == EFF_SWEEPUP || Effect == EFF_SWEEPDOWN))
		Effect = Effect == EFF_SWEEPUP ? EFF_FDS_MOD_DEPTH : EFF_FDS_MOD_SPEED_HI;
	else if (S5B && (Effect == EFF_SWEEPUP || Effect == EFF_SWEEPDOWN || Effect == EFF_FDS_MOD_SPEED_LO))
		Effect = Effect == EFF_SWEEPUP ? EFF_SUNSOFT_ENV_TYPE : Effect == EFF_SWEEPDOWN ? EFF_SUNSOFT_ENV_HI : EFF_SUNSOFT_ENV_LO;
	switch (Effect) {
	case EFF_ARPEGGIO: {
		if (P == 0)
			return "Arpeggio off: the note plays alone again";
		const std::string Chord = ArpeggioChord(x, y);
		std::string Text = y == 0 ? "Arpeggio: the note and the one " + Number(x) + " semitones up, in turn" :
			x == 0 ? "Arpeggio: the note and the one " + Number(y) + " semitones up, in turn" :
			"Arpeggio: the note and the notes " + Number(x) + " and " + Number(y) + " semitones up, in turn";
		return Chord.empty() ? Text + ", a quick chord" : Text + ": " + Chord;
	}
	case EFF_PORTA_UP:
		return P == 0 ? "Slide up off" : "Slide up: the pitch keeps climbing, speed " + Number(P);
	case EFF_PORTA_DOWN:
		return P == 0 ? "Slide down off" : "Slide down: the pitch keeps falling, speed " + Number(P);
	case EFF_PORTAMENTO:
		return P == 0 ? "Portamento off: notes jump to their pitch again" :
			"Portamento: each new note glides from the one before, speed " + Number(P);
	case EFF_VIBRATO:
		if (y == 0)
			return "Vibrato off";
		return "Vibrato: the pitch wobbles, speed " + Number(x) + (x == 0 ? " (it doesn't move)" : "") + ", depth " + Number(y);
	case EFF_TREMOLO:
		if (y == 0)
			return "Tremolo off";
		return "Tremolo: the volume wobbles, speed " + Number(x) + ", depth " + Number(y);
	case EFF_PAN:
		if (P == 0x80)
			return "Pan: the middle";
		if (P == 0x00)
			return "Pan: hard left";
		if (P == 0xFF)
			return "Pan: hard right";
		if (P < 0x80)
			return "Pan: to the left, " + Number((0x80 - P) * 100 / 128) + "% of the way";
		return "Pan: to the right, " + Number((P - 0x80) * 100 / 127) + "% of the way";
	case EFF_VOLUME_SLIDE:
		if (P == 0)
			return "Volume slide off";
		if (x == 0)
			return "Volume slide: fading down, speed " + Number(y);
		if (y == 0)
			return "Volume slide: swelling up, speed " + Number(x);
		return "Volume slide: up by " + Number(x) + " and down by " + Number(y) + " at once (use one of them)";
	case EFF_DELAY:
		return P == 0 ? "Note delay of 0 ticks: the note starts on time" :
			"Note delay: this row's note starts " + Number(P) + (P == 1 ? " tick" : " ticks") + " late";
	case EFF_SWEEPUP:
	case EFF_SWEEPDOWN: {
		std::string Text = Effect == EFF_SWEEPUP ? "Sweep up: the pulse's pitch rises by itself" : "Sweep down: the pulse's pitch falls by itself";
		static const char *const HOW_OFTEN[] = { "very often", "very often", "often", "often", "now and then", "now and then",
			"slowly", "slowly" };
		static const char *const HOW_FAR[] = { "not at all", "in big steps", "in large steps", "in large steps", "in small steps",
			"in small steps", "in tiny steps", "in tiny steps" };
		Text += ", " + std::string(HOW_OFTEN[x & 7]) + " (x " + Number(x) + ") " + HOW_FAR[y & 7] + " (y " + Number(y) + ")";
		if (x > 7 || y == 0)
			Text += "; x goes 0-7 and y 1-7";
		if (!Is2A03Pulse(Channel) && !Channel.Chip.empty())
			Text += " (only the 2A03's pulses sweep)";
		return Text;
	}
	case EFF_PITCH:
		if (P == 0x80)
			return "Fine pitch: in tune";
		return "Fine pitch: a little " + std::string(P > 0x80 ? "higher" : "lower") + ", " + Number(P > 0x80 ? P - 0x80 : 0x80 - P) +
			(std::abs(P - 0x80) == 1 ? " step" : " steps");
	case EFF_SLIDE_UP:
	case EFF_SLIDE_DOWN:
		if (y == 0)
			return std::string(Effect == EFF_SLIDE_UP ? "Note slide up" : "Note slide down") + " of 0 semitones: nothing moves";
		return std::string(Effect == EFF_SLIDE_UP ? "Note slide up: " : "Note slide down: ") + Number(y) + (y == 1 ? " semitone" : " semitones") +
			(Effect == EFF_SLIDE_UP ? " up" : " down") + ", speed " + Number(x);
	case EFF_NOTE_CUT:
		return "Note cut: the note stops after " + Number(P) + (P == 1 ? " tick" : " ticks");
	case EFF_DUTY_CYCLE:
		if (Channel.Kind == KIND_PULSE && Channel.Chip == "VRC6") {
			const int Sixteenths = (P & 7) + 1;
			return "Pulse width: " + Number(Sixteenths) + "/16" + (Sixteenths == 8 ? ", a square: round and hollow" :
				Sixteenths <= 2 ? ": thin and nasal" : Sixteenths <= 4 ? ": bright" : ": full");
		}
		if (Channel.Kind == KIND_PULSE) {
			static const char *const WIDTHS[] = { "12.5%: thin and nasal", "25%: bright", "50%, a square: round and hollow",
				"75%: bright, like 25%" };
			return std::string("Pulse width: ") + WIDTHS[P & 3];
		}
		if (Channel.Kind == KIND_NOISE)
			return (P & 1) != 0 ? "Noise mode: metallic, a buzzing tone" : "Noise mode: hiss";
		if (Channel.Chip == "N163")
			return "Wave: the instrument's wave " + Number(P);
		if (S5B) {
			std::string Plays;
			if (P & 1)
				Plays = "the square";
			if (P & 2)
				Plays += std::string(Plays.empty() ? "" : " and ") + "the noise";
			if (P & 4)
				Plays += std::string(Plays.empty() ? "" : " and ") + "the envelope";
			return "Plays: " + (Plays.empty() ? std::string("nothing") : Plays);
		}
		return "Timbre: " + Hex(P) + " (this channel has no widths or modes)";
	case EFF_DPCM_PITCH:
		return "DPCM pitch: rate " + Number(P & 15) + " of 15, higher is faster";
	case EFF_RETRIGGER:
		return "DPCM retrigger: the sample starts again every " + Number(P + 1) + " ticks";
	case EFF_SAMPLE_OFFSET:
		return "Sample offset: the sample starts " + Number(P * 64) + " bytes in";
	case EFF_DAC:
		return "DPCM level: " + Number(P & 0x7F) + " of 127" + (P == 0 ? " (the triangle and the noise at full volume)" :
			", the triangle and the noise a little quieter");
	case EFF_FDS_MOD_DEPTH:
		return "FDS modulation depth: " + Number(P & 0x3F) + " of 63";
	case EFF_FDS_MOD_SPEED_HI:
		return "FDS modulation speed, coarse: " + Number(P & 15);
	case EFF_FDS_MOD_SPEED_LO:
		return Fds || Channel.Chip.empty() ? "FDS modulation speed, fine: " + Number(P) :
			"Jxx: the FDS's modulation speed, or the 5B's envelope; this channel has neither";
	case EFF_SUNSOFT_ENV_TYPE:
		return "5B envelope: " + std::string(EnvelopeShape(y)) + (x == 0 ? ", at the speed Ixx and Jxx set" : x == 12 ?
			", following the note at its own pitch (a buzz)" : x == 11 ? ", following the note an octave under it" :
			x == 13 ? ", following the note an octave over it" : ", following the note");
	case EFF_SUNSOFT_ENV_HI:
		return "5B envelope speed, coarse part: " + Number(P) + " (bigger is slower)";
	case EFF_SUNSOFT_ENV_LO:
		return "5B envelope speed, fine part: " + Number(P);
	case EFF_SPEED:
		if (P == 0)
			return "Speed: F00 does nothing";
		return P < SpeedSplit ? "Speed " + Number(P) + ": each row lasts " + Number(P) + (P == 1 ? " tick" : " ticks") :
			"Tempo " + Number(P);
	case EFF_JUMP:
		return "Jump: after this row the song goes on at frame " + Hex(P);
	case EFF_SKIP:
		return "Skip: after this row the next frame starts, at its row " + Hex(P);
	case EFF_HALT:
		return "Halt: the song stops after this row";
	case EFF_VOLUME:
		return "Volume (an old effect): " + Number(P) + "; the volume column does this now";
	default:
		return std::string();
	}
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
		const std::string Own = LetterChip(pChannel);
		for (size_t i = 0; i < Effects.size(); ++i) {
			const EffectInfo &E = Effects[i];
			if (E.Form[0] != First)
				continue;
			// H, I and J: the sweep on the 2A03 pulses, the modulator on the FDS, the envelope on the 5B (J: the FDS's and
			// the 5B's, unless the channel is one of theirs)
			if ((First == 'H' || First == 'I' || (First == 'J' && !Own.empty())) && EntryChip(E) != Own)
				continue;
			Text += (Text.empty() ? "" : "\n") + std::string(E.Form) + " " + E.Name + ": " + E.Does + " Channels: " + E.Channels +
				". Lasts: " + E.Lasts + ". Good for: " + E.Uses;
		}
		if ((First == 'H' || First == 'I') && Own.empty())
			Text += std::string("\n(On the FDS channel, ") + First + "xx is its modulator, and on the 5B's its envelope: ask with "
				"that channel.)";
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
		const std::string Own = LetterChip(&C);
		for (size_t i = 0; i < Effects.size(); ++i) {
			const EffectInfo &E = Effects[i];
			// H, I and J: the sweep on the 2A03 pulses, the modulator on the FDS, the envelope on the 5B
			const char L = E.Form[0];
			if ((L == 'H' || L == 'I' || L == 'J') && EntryChip(E) != Own)
				continue;
			const bool Pair = L == 'Q' || L == 'R' || (Own.empty() && (L == 'H' || L == 'I'));
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
		if (C.Chip == "5B")
			Text += " Hxy, Ixx and Jxx steer the 5B's envelope, which its three channels share.";
		if (MostTimbre(C) >= 0) {
			char Line[96];
			std::snprintf(Line, sizeof(Line), " Vxx: V00-V%02X%s.", MostTimbre(C), C.Chip == "5B" ?
				" (1 the square, 2 the noise, 4 the envelope, added up)" : "");
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
	else if (Channel.Chip == "5B" && (Letter == 'H' || Letter == 'I' || Letter == 'J'))
		Effect = Letter == 'H' ? EFF_SUNSOFT_ENV_TYPE : (Letter == 'I' ? EFF_SUNSOFT_ENV_HI : EFF_SUNSOFT_ENV_LO);
	else if (Letter == 'J')
		Effect = EFF_FDS_MOD_SPEED_LO;	// its ChannelProblem says it's the FDS's and the 5B's
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

bool ChannelPlaysEffect(const ChannelInfo &Channel, int Effect, int Param)
{
	return ChannelProblem(Channel, Effect, Param, 0, std::string()).empty();
}

}	// namespace ftapi

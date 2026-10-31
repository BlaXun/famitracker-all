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

#include "WhatNext.h"
#include "SongSeed.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ftapi {

// A dry run: reading goes to the song, changing is counted and kept back

bool DryRunHost::GetSong(SongInfo &Info, std::string &Error)
{
	if (!m_Host.GetSong(Info, Error))
		return false;
	Info.DirectChanges = true;	// so a tool writes (here: counts) rather than making a clip
	return true;
}

void DryRunHost::ReadCells(int Channel, int First, int Rows, std::vector<Cell> &Out)
{
	m_Host.ReadCells(Channel, First, Rows, Out);
	if (!Overlay)
		return;
	// The cells as the writes kept back would leave them, as FamiTracker writes a part (CInsertRecordingAction): an exact
	// part's cells as they are, another's notes, volumes and effects with the song's own effects (speed, tempo, jumps)
	// kept where they were. Writes that move frames aren't followed.
	if (Out.size() < static_cast<size_t>(std::max(0, Rows)))
		Out.resize(static_cast<size_t>(std::max(0, Rows)));
	for (size_t w = 0; w < Writes.size(); ++w) {
		const SongWrite &Write = Writes[w].first;
		if (Write.MoveBy != 0)
			continue;
		for (size_t p = 0; p < Writes[w].second.size(); ++p) {
			const ChannelCells &Part = Writes[w].second[p];
			if (Part.Channel != Channel)
				continue;
			for (size_t r = 0; r < Part.Cells.size(); ++r) {
				const int Row = Write.First + static_cast<int>(r);
				if (Row < First || Row >= First + Rows)
					continue;
				Cell &Old = Out[static_cast<size_t>(Row - First)];
				const Cell &New = Part.Cells[r];
				if (Part.Exact) {
					Old = New;
					continue;
				}
				Cell Kept = New;
				for (int k = 0; k < EFFECT_COLUMNS; ++k)
					if (IsSongEffect(Old.Effect[k])) {
						int Column = Kept.Effect[k] == EFF_NONE ? k : -1;
						for (int j = 0; j < EFFECT_COLUMNS && Column < 0; ++j)
							if (Kept.Effect[j] == EFF_NONE)
								Column = j;
						if (Column >= 0) {
							Kept.Effect[Column] = Old.Effect[k];
							Kept.Param[Column] = Old.Param[k];
						}
					}
				Old = Kept;
			}
		}
	}
}

bool DryRunHost::ReadClip(int Index, std::vector<std::vector<Cell> > &Columns)
{
	return m_Host.ReadClip(Index, Columns);
}

bool DryRunHost::CanChange(std::string &)
{
	return true;
}

bool DryRunHost::WriteSong(const std::vector<ChannelCells> &Parts, const SongWrite &Write, std::string &)
{
	++Changes;
	Writes.push_back(std::make_pair(Write, Parts));
	return true;
}

bool DryRunHost::SetSections(const SectionList &, std::string &)
{
	++Changes;
	++Others;
	return true;
}

bool DryRunHost::SetKey(bool, const KeyChoice &, std::string &)
{
	++Changes;
	++Others;
	return true;
}

bool DryRunHost::SetMixer(const MixerSettings &, std::string &)
{
	++Changes;
	++Others;
	return true;
}

bool DryRunHost::AddClip(std::string &, const std::vector<int> &, const std::vector<std::vector<Cell> > &, std::string &)
{
	++Changes;
	++Others;
	return true;
}

namespace {

const char *const DRY_RUN = "Not in a dry run.";

}	// namespace

int DryRunHost::AddInstrument(const Preset &, const std::string &, bool, std::string &Error)
{
	Error = DRY_RUN;
	return -1;
}

int DryRunHost::AddN163Instrument(const WaveSound &, int, std::string &Error)
{
	Error = DRY_RUN;
	return -1;
}

int DryRunHost::AddFdsInstrument(const FdsSound &, std::string &Error)
{
	Error = DRY_RUN;
	return -1;
}

int DryRunHost::AddVrc7Instrument(int, const std::string &, std::string &Error)
{
	Error = DRY_RUN;
	return -1;
}

int DryRunHost::AddVrc7CustomInstrument(const std::vector<int> &, const std::string &, std::string &Error)
{
	Error = DRY_RUN;
	return -1;
}

int DryRunHost::AddDpcmKit(const DpcmKit &, std::string &Error)
{
	Error = DRY_RUN;
	return -1;
}

int DryRunHost::RefreshDpcmKit(const DpcmKit &, std::string &)
{
	return 0;	// nothing brought up to date
}

void DryRunHost::CountInstrumentUses(std::vector<int> &InTracks, std::vector<int> &InClips)
{
	m_Host.CountInstrumentUses(InTracks, InClips);
}

bool DryRunHost::RemoveInstruments(const std::vector<int> &, std::string &Error)
{
	Error = DRY_RUN;
	return false;
}

int DryRunHost::ExpansionNotes(const std::vector<std::string> &Chips, int Channels, std::vector<int> &Tracks)
{
	return m_Host.ExpansionNotes(Chips, Channels, Tracks);
}

bool DryRunHost::SetExpansion(const std::vector<std::string> &, int, int &Moved, int &Lost, std::string &Error)
{
	Moved = Lost = 0;
	Error = DRY_RUN;
	return false;
}

bool DryRunHost::SetSpeedTempo(int, int, std::string &)
{
	++Changes;
	++Others;
	return true;
}

bool DryRunHost::SetMeter(int, int, int, std::string &)
{
	++Changes;
	++Others;
	return true;
}

bool DryRunHost::SetNames(const SongNames &, std::string &)
{
	++Changes;
	++Others;
	return true;
}

bool DryRunHost::SelectTrack(int, std::string &Error)
{
	Error = DRY_RUN;
	return false;
}

int DryRunHost::AddTrack(const std::string &, const std::vector<std::vector<Cell> > &, int, int, std::string &Error)
{
	Error = DRY_RUN;
	return -1;
}

bool DryRunHost::RenderTrack(int Track, double Seconds, const RenderOptions &Options, std::vector<float> &Left,
	std::vector<float> &Right, int &SampleRate, std::vector<std::pair<int, long long> > &Marks, std::string &Error)
{
	return m_Host.RenderTrack(Track, Seconds, Options, Left, Right, SampleRate, Marks, Error);
}

std::unique_ptr<RenderSet> DryRunHost::RenderParts(int Track, double Seconds, const std::vector<RenderOptions> &Renders,
	std::string &Error)
{
	return m_Host.RenderParts(Track, Seconds, Renders, Error);
}

bool DryRunHost::SaveFile(const std::string &FileName, const std::string &, std::string &Path, std::string &)
{
	++Changes;
	++Others;
	Path = FileName;
	return true;
}

bool DryRunHost::ReadFile(const std::string &FileName, std::string &Bytes, std::string &Error)
{
	return m_Host.ReadFile(FileName, Bytes, Error);
}

std::string DryRunHost::LocalPath(const std::string &Path)
{
	return m_Host.LocalPath(Path);
}

bool DryRunHost::SaveInbox(const std::string &, const std::string &, std::string &, std::string &Error)
{
	Error = DRY_RUN;
	return false;
}

bool DryRunHost::ReadExport(const std::string &Name, std::string &Bytes, std::string &Error)
{
	return m_Host.ReadExport(Name, Bytes, Error);
}

bool DryRunHost::ListLibrary(std::vector<std::string> &Names, std::string &Error)
{
	return m_Host.ListLibrary(Names, Error);
}

bool DryRunHost::ReadLibrary(const std::string &Name, std::string &Bytes, std::string &Error)
{
	return m_Host.ReadLibrary(Name, Bytes, Error);
}

bool DryRunHost::SaveLibrary(const std::string &, const std::string &, std::string &, std::string &Error)
{
	Error = DRY_RUN;
	return false;
}

bool DryRunHost::DeleteLibrary(const std::string &, std::string &Error)
{
	Error = DRY_RUN;
	return false;
}

bool DryRunHost::LatestRecording(std::string &Path, std::string &Error)
{
	return m_Host.LatestRecording(Path, Error);
}

bool DryRunHost::OpenText(const std::string &, std::string &Error)
{
	Error = DRY_RUN;
	return false;
}

bool DryRunHost::Play(const PlayRequest &, std::string &Error)
{
	Error = DRY_RUN;
	return false;
}

bool DryRunHost::Undo(std::string &Message)
{
	Message = DRY_RUN;
	return false;
}

bool DryRunHost::Save(const std::string &, std::string &Message)
{
	Message = DRY_RUN;
	return false;
}

// The kinds of song, as the recipes (get_recipe, compose_track) and the style guide's topics have them: their tempos,
// the lead's sound, how the chords, the bass and the drums play, the mix and the export's loudness. A lively stage's
// are the soundtrack's first lively stages' (the user liked nearly all of them): a lead that blooms in and settles, fast
// lush arpeggios, a sampled backbeat (the basic groove with kit samples). Their pools (from feat-cozy-kind) start with
// those, which a song without a seed keeps, and add alternatives for seeded songs. A calm song's lead is the night songs' best (the
// calm recipe's first choice): a music box on a VRC6 pulse at 37.5% that blooms in and rings away within a beat. A cozy
// place (a shop, a hut, an inn: warm, indoors, daytime, a friendly face) is neither a night song nor a stage: "Lost n
// Found" (a run-down hut where an old woman keeps the lost things), asked for as calm, came out a night song in C minor and
// sounded depressing. So cozy has keys of its own, all major (F and Bb are the warm ones), a walking pace between calm's
// and a stage's (100 BPM is speed 9 at tempo 150, 90 and 112.5 the speeds either side), a lead struck warm that falls to a
// low hold and keeps singing (falling strike: not calm's music box, which rings away, nor a stage's bloom lead, which
// holds high), broken chords under it, a bass on root and fifth that bounces (walking when it moves), and the lively
// groove, the soft one (hats and a soft kick, no snare), on the noise alone. It mixes and checks in calm's style: calm's
// places were the night songs' liked balance (the melody 2-3 dB over the chords), its drums stay as soft as written, its
// polish adds no accents and its checks find a lead that holds a high level. It exports at -20 LUFS, as most of a game's
// music does: it plays beside the towns and the stages, where a night song's -22 is for the quiet scenes
namespace {

const NextKind KINDS[] = {
	{ NEXT_GENERAL, "general", 0.0, 0.0, 150.0, "Melody lead", "lead", "arpeggio", "arpeggio", "arpeggio", "basic", "soft", "any",
		-20.0, { "C major", "G major", "D major" }, 0, 0, 0, 0, false, 0, 0, 0, 0, { 0, 0 }, { 0, 0 }, 0, 0, 0, 0,
		"lead|bloom lead|keys", "arpeggio|broken|stabs", "arpeggio|walking", "basic|four_on_floor|shuffle", "150|128.6", 0 },
	{ NEXT_CALM, "calm", 60.0, 90.0, 75.0, "Calm lead", "music box", "lush", "roots", "half", "", "calm", "calm", -22.0,
		{ "F major", "D major", "G major" }, 0, 0, 0, 0, false, 0, 0, 0, 0, { 0, 0 }, { 0, 0 }, 0, 0, 0, 0,
		"music box|breath|falling strike", "lush|broken", "roots|half", "", "75|69.2|81.8", 0 },
	{ NEXT_LIVELY, "lively", 105.0, 165.0, 128.6, "Stage lead", "bloom lead", "lush", "arpeggio", "arpeggio", "basic", "lively",
		"lively", -20.0, { "G major", "D major", "A major" }, 0, 0, 0, 0, false, 0, 0, 0, 0, { 0, 0 }, { 0, 0 }, 0, 0, 0, 0,
		"bloom lead|lead", "lush|broken|arpeggio", "arpeggio|octaves", "basic|four_on_floor", "128.6|150|112.5", 0 },
	{ NEXT_ACTION, "action", 135.0, 195.0, 150.0, "Action lead", "bloom lead", "stabs", "octaves", "octaves", "driving", "soft", "any",
		-19.0, { "A minor", "D minor", "E minor" }, 0, 0, 0, 0, false, 0, 0, 0, 0, { 0, 0 }, { 0, 0 }, 0, 0, 0, 0,
		"bloom lead|stepped lead", "stabs|arpeggio", "octaves|driving", "driving|basic", "150|180", 0 },
	{ NEXT_COZY, "cozy", 85.0, 112.5, 100.0, "Cozy lead", "falling strike", "broken", "root_fifth", "walking", "lively", "calm", "calm",
		-20.0, { "F major", "Bb major", "C major" }, "cosy warm homey homely indoors shop inn house", 1, 0, 0, false, 0, 0, 0, 0,
		{ 0, 0 }, { 0, 0 }, 0, 0, 0, 0,
		"falling strike|keys|breath", "broken|lush|stabs", "root_fifth|walking|half", "lively|calm|", "100|90|112.5", "F major|Bb major|C major|G major|D major" },
	// Caves, sewers, ruins, forgotten places: curious rather than scary. Minor or dorian (not sad: dorian's raised sixth;
	// its chords the ones both share, i III v VII, so a dorian key's B major doesn't put a D# over the D drone),
	// a slow walk (90 BPM, speed 10), a lead that breathes in over a drone (the pedal bass) with soft, slow chords, the
	// tune sparse with long notes and gaps for the echo, no beat; calm's style and checks; -21 LUFS, a little under a stage
	{ NEXT_MYSTERIOUS, "mysterious", 85.0, 100.0, 90.0, "Mystery lead", "breath", "lush", "pedal", "roots", "", "calm", "calm", -21.0,
		{ "D dorian", "A minor", "E dorian" }, "mystery mystic cave caves sewer sewers ruin ruins crypt hidden secret", -1, "plain",
		"", false, 1, "a mysterious track", "the tune's long notes over the drone, the echo answering in the gaps, nothing louder "
		"than a curious step",
		"a mysterious place's lead breathes in and holds low (kind breath): long notes over the drone that leave gaps for "
		"the echo, curious rather than scary.", { 0, 0 },
		{ "| i | VII | III | VII | i | VII | v | VII |", "| III | VII | i | v | III | VII | v | v |" },
		"caves, sewers, ruins, forgotten places", "curious rather than scary: a place that hides something, minor or dorian but "
		"not sad, slow and spacious, a drone underneath and an echo in the gaps",
		"sparse: 1-3 notes a bar, long notes and gaps of a beat or more for the echo to answer, steps and a rising fourth "
		"or a tritone's lean now and then, the high note rare",
		"the echo joining in the repeat, a second voice in long notes, a bar of the drone alone in the break; no backbeat (a "
		"soft tick on the noise at most)",
		"breath|falling strike|music box", "lush|broken", "pedal|roots", "|calm", "90|100", "D dorian|A minor|E dorian|G dorian|E minor" },
	// Open fields, travel, an airship flight: a wide tune that leaps upward over a rolling bass and a steady march, major,
	// 128.6 BPM (speed 7); a lively stage's band (lively's style, mix and checks), broken chords plain under the leaps,
	// the march groove with kit samples; -20 LUFS
	{ NEXT_ADVENTURE, "adventure", 110.0, 132.0, 128.6, "Adventure lead", "bloom lead", "broken", "arpeggio", "octaves", "basic",
		"lively", "lively", -20.0, { "D major", "G major", "Bb major" }, "adventurous journey travel travelling traveling voyage "
		"overworld airship flight flying", 1, "plain", "", true, 2, "an adventure track", "the tune leaping wide over the band, "
		"the bass rolling under it and the march steady",
		"an adventure's lead blooms in and settles (kind bloom lead), bold enough for a tune that leaps upward.",
		{ "| I | V | IV | V | I | vi | IV | V |", "| IV | I | V | vi | IV | I | ii | V |" }, { 0, 0 },
		"open fields, travel, an airship flight", "wide and hopeful: the road ahead, a tune that leaps upward over a rolling bass "
		"and a steady march",
		"wide: a leap up of a fifth or a sixth (an octave at the high point) and steps back down, phrases of 2 or 4 bars that "
		"end on a held note, a dotted rhythm that marches, mostly octave 3, reaching into 4 at each part's high point",
		"a harmony a third or sixth under the tune in the repeat, the drums busier, the bass in octaves for the last time "
		"round, a fill and a crash into each part",
		"bloom lead|lead|brass", "broken|arpeggio|stabs", "arpeggio|octaves", "basic|four_on_floor", "128.6|112.5", "D major|G major|Bb major|A major|F major" },
	// The sky, clouds, underwater, waiting at a dock: a slow pulse (90 BPM, speed 10), major (lydian's raised fourth for
	// color), rolling broken chords, a bass on half bars, a lead that breathes, no drums, an echo; calm's style; -21 LUFS
	{ NEXT_FLOATING, "floating", 80.0, 95.0, 90.0, "Floating lead", "breath", "broken", "half", "roots", "", "calm", "calm", -21.0,
		{ "F major", "D major", "G major" }, "float floaty afloat sky skies cloud clouds underwater drifting drift", 1, "plain", "",
		false, 1, "a floating track", "the arpeggios rolling on like water, the tune drifting a little over them, the echo "
		"wide",
		"a floating lead breathes in and holds a soft level (kind breath), drifting over rolling chords.",
		{ "| I | iii | IV | I | vi | iii | IV | IV |", "| IV | I | vi | V | IV | I | ii | IV |" }, { 0, 0 },
		"the sky, clouds, underwater, waiting at a dock", "weightless: a slow pulse, arpeggios rolling on like water or "
		"wind, the tune drifting over them, plenty of echo, major (or lydian, its raised fourth floating)",
		"drifting: long notes over the rolling chords, 2-3 a bar, rising and settling by step, a phrase of two bars that ends "
		"held, as if carried",
		"an echo a beat behind from the first repeat, a second voice in long notes, the arpeggios moved up an octave in B; "
		"no drums (a soft shaker of hi-hats at most)",
		"breath|music box|falling strike", "broken|lush", "half|roots|pedal", "", "90|81.8", "F major|D major|G major|C major" },
	// Minigames, funny characters, a toy shop: major and bouncy, short cheeky phrases, stabs on the offbeats over a
	// root-and-fifth bass (polish's staccato makes it hop), light drums; the general recipe's style and mix, -20 LUFS
	{ NEXT_PLAYFUL, "playful", 112.5, 150.0, 128.6, "Playful lead", "lead", "stabs", "root_fifth", "walking", "basic", "soft",
		"any", -20.0, { "C major", "G major", "F major" }, "silly funny cheeky goofy toy toys minigame minigames", 1, "plain", "light",
		false, 2, "a playful track", "the tune hopping and cheeky, the stabs and the bass bouncing, the drums light",
		"a playful lead speaks at once and settles a little under its peak (kind lead), so short, cheeky notes hop "
		"and none vanishes.",
		{ "| I | IV | V | I | I | IV | ii | V |", "| IV | I | IV | V | vi | ii | V | V |" }, { 0, 0 },
		"minigames, funny characters, a toy shop", "bouncy and cheeky: short phrases that hop, a staccato bass, light drums, a "
		"swing or a 6/8 lilt if it suits",
		"short cheeky phrases of a bar or two, repeated notes and skips, a rest on a beat now and then, a grace note's hop, "
		"the answer turning the question around",
		"the repeat with a cheeky variation (vary_part), a second voice in the B part, a fill where each phrase ends",
		"lead|bloom lead", "stabs|broken|arpeggio", "root_fifth|walking|octaves", "basic|calm|shuffle", "128.6|150|112.5", "C major|G major|F major|D major" },
	// A market, a festival, a holiday town: fast and bright, stabs, a catchy tune (busy, but not too busy for the NES:
	// check_song's too_busy), fuller drums (the basic groove with kit samples), lively's style, -19 LUFS
	{ NEXT_FESTIVE, "festive", 125.0, 165.0, 150.0, "Festive lead", "bloom lead", "stabs", "octaves", "walking", "basic", "lively",
		"lively", -19.0, { "D major", "A major", "G major" }, "festival fair carnival celebration party holiday parade feast", 1,
		"plain", "", true, 2, "a festive track", "the tune catchy over bright stabs, the drums full, the bass in octaves",
		"a festive lead blooms in and settles (kind bloom lead), bright enough to lead a crowd.",
		{ "| I | IV | V | I | I | IV | V | V |", "| IV | V | iii | vi | IV | V | I | V |" }, { 0, 0 },
		"a market day, a festival, a holiday town", "fast and bright: a catchy tune over bright stabs and full drums",
		"catchy: a motif of 4-6 notes that comes back in every phrase, two or three rhythms repeated (never a new one each "
		"bar: too busy for the NES), phrases of 2 or 4 bars",
		"a harmony in thirds in the repeat, busier drums and a crash, the bass in octaves",
		"bloom lead|brass|lead", "stabs|arpeggio|broken", "octaves|walking", "basic|four_on_floor", "150|128.6", "D major|A major|G major|C major" },
	// A farewell, a memorial, a sunset, an ending: major with borrowed minor chords (iv, bVI), slow, a singing tune. Never
	// plainly minor and gloomy: its keys are major and a minor one gets what_next's note, as the recipe says why
	{ NEXT_BITTERSWEET, "bittersweet", 70.0, 90.0, 75.0, "Bittersweet lead", "breath", "lush", "roots", "half", "", "calm", "calm",
		-22.0, { "F major", "C major", "G major" }, "farewell goodbye memorial sunset nostalgic nostalgia", 1, "plain", "", false, 1,
		"a bittersweet track", "the tune singing over chords that turn minor for a moment (iv, bVI) and come home to major",
		"a bittersweet lead breathes in and sings (kind breath): long notes that ache a little.",
		{ "| I | iii | IV | iv | I | vi | bVI | V |", "| vi | IV | iv | I | bVI | IV | iv | V |" }, { 0, 0 },
		"a farewell, a memorial, a sunset, an ending", "fond and a little sad, never gloomy: a major key whose borrowed minor "
		"chords (iv, bVI) ache for a moment and come home (a plain minor key turns it into a lament, which isn't this)",
		"a singing tune: long notes, a sigh (a falling second) at a phrase's end, a leap up of a sixth onto the borrowed "
		"chord, phrases of two bars ending held",
		"an echo a beat behind in the repeat, a second voice in long notes, the last chord a major one",
		"breath|falling strike|music box", "lush|broken", "roots|half", "", "75|81.8", "F major|C major|G major|Bb major" },
	// Sneaking, a guarded place, slow danger: minor, 100 BPM, an ostinato bass (driving), sparse stabs, a tune without big
	// leaps (grows as calm: by step), soft drums or ticking hats; not action, which is a fight; -20 LUFS
	{ NEXT_TENSE, "tense", 90.0, 112.5, 100.0, "Tense lead", "stepped lead", "stabs", "driving", "syncopated", "calm", "soft", "any",
		-20.0, { "E minor", "A minor", "D minor" }, "tension sneak sneaking stealth stealthy guarded prison", -1, "plain", "light",
		false, 1, "a tense track", "the ostinato under everything, the stabs sparse, the tune close and quiet, the hats ticking",
		"a tense lead holds, then steps down (kind stepped lead): close, careful notes.",
		{ 0, 0 }, { "| i | i | VI | V | i | i | iv | V |", "| iv | iv | i | i | VI | VI | V | V |" },
		"sneaking, a guarded place, danger that is slow", "watchful: an ostinato bass that never stops, sparse stabs, a "
		"close tune without big leaps, ticking hats; not a fight (that's action)",
		"close: steps and repeated notes, a semitone's lean, no leap past a fourth, rests where a guard might look up",
		"the repeat a step higher or with the ostinato doubled, a second voice a third under in long notes",
		"stepped lead|lead", "stabs|broken", "driving|syncopated", "calm", "100|90|112.5", "E minor|A minor|D minor|B minor" },
	// Openly sad (grief, loneliness, rain, an abandoned home, a lost friend): minor (natural or dorian; it may stay minor,
	// unlike bittersweet), slow (75 BPM, speed 12), a singing lead with long notes and sighs, sparse soft chords, no drums;
	// calm's style, -22 LUFS. Only when sadness is asked for: its words are sadness's own, never neglect's (run-down, old)
	{ NEXT_MELANCHOLIC, "melancholic", 60.0, 80.0, 75.0, "Melancholic lead", "breath", "lush", "roots", "half", "", "calm", "calm",
		-22.0, { "A minor", "D dorian", "E minor" }, "melancholy sad sorrow sorrowful grief lament mournful lonely", -1, "plain", "",
		false, 1, "a melancholic track", "the tune singing alone over the soft chords, its sighs falling, nothing hurrying it",
		"a melancholic lead breathes in and sings (kind breath): long notes that sigh.", { 0, 0 },
		{ "| i | III | VII | v | i | III | VII | VII |", "| III | VII | v | i | III | VII | v | v |" },
		"grief, loneliness, rain, an abandoned home, a lost friend", "openly sad: a slow, singing tune with long notes and "
		"sighs over sparse soft chords, no drums; it may stay minor (unlike bittersweet, which borrows its sadness)",
		"singing: long notes, sighs (a falling second, the upper note on the beat) at the phrases' ends, a slow climb to one "
		"high note a section, 1-3 notes a bar",
		"the tune alone for a phrase, an echo far behind in the repeat, a second voice in long notes, the chords thinning",
		"breath|falling strike|music box", "lush|broken", "roots|half", "", "75|69.2|64.3", "A minor|D dorian|E minor|C minor" },
	// Texture over tune (Metroid's ambience): drones, slow pads, sparse motifs rather than a tune to hum, an echo, no
	// drums, minor or modal, 60-90 BPM, loose long sections (the night's shape). Its style, atmospheric, is calm's with the
	// checks that ask for a lead (phrases, breaths, a covered melody) left out, as a cue's checks leave out a loop's
	{ NEXT_ATMOSPHERIC, "atmospheric", 60.0, 90.0, 75.0, "Texture lead", "breath", "lush", "pedal", "roots", "", "calm",
		"atmospheric", -22.0, { "E minor", "D dorian", "A minor" }, "atmosphere ambience texture vast void abyss", 0, "plain", "",
		false, 1, "an atmospheric track", "the drone and the pads changing slowly, the motifs few and far apart, the echo wide",
		"an atmospheric track's motifs breathe in and hold (kind breath): sparse, a texture's figures rather than a tune.",
		{ 0, 0 }, { "| i | i | VII | VII | i | i | III | VII |", "| III | III | VII | VII | v | v | VII | VII |" },
		"vast, empty, alien places, the deep, an abyss", "texture over tune: a drone, slow-changing pads, sparse motifs rather "
		"than a melody to hum, an echo, quiet or no percussion, the form loose",
		"sparse motifs of 2-4 notes, far apart (a bar or two of rests between them), long notes, intervals of a fourth or a "
		"fifth, no hook to hum",
		"the pads changing chord slowly, the motif answered by its echo, a part dropping out; drums none, or a soft, "
		"irregular tick",
		"breath|falling strike", "lush|broken", "pedal|roots", "", "75|64.3|81.8", "E minor|D dorian|A minor|C minor" },
};

}	// namespace

const NextKind &NextKindInfo(int Kind)
{
	for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); ++i)
		if (KINDS[i].Kind == Kind)
			return KINDS[i];
	return KINDS[0];
}

std::vector<std::string> PoolEntries(const char *Pool)
{
	std::vector<std::string> Out;
	if (Pool == 0)
		return Out;
	std::string Item;
	for (const char *p = Pool; ; ++p) {
		if (*p == '|' || *p == 0) {
			Out.push_back(Item);
			Item.clear();
			if (*p == 0)
				break;
		}
		else
			Item += *p;
	}
	return Out;
}

std::string PoolText(const char *Pool, const char *Single)
{
	std::vector<std::string> All = Pool != 0 ? PoolEntries(Pool) : std::vector<std::string>(1, Single != 0 ? Single : "");
	std::string Out;
	for (size_t i = 0; i < All.size(); ++i)
		Out += (i == 0 ? "" : i + 1 == All.size() ? " or " : ", ") + (All[i].empty() ? std::string("none") : All[i]);
	return Out;
}

namespace {

NextPick PickOf(const char *Pool, const std::string &Single, const char *Salt, const std::string &Name, unsigned int Seed)
{
	NextPick Out;
	const std::vector<std::string> All = PoolEntries(Pool);
	if (All.empty()) {
		Out.Value = Single;
		return Out;
	}
	Out.Count = static_cast<int>(All.size());
	// (no seed: the kind's own, the pool's first, as before songs had seeds, so a song made then keeps its picks)
	Out.Value = Seed == 0 ? All[0] : All[SeededHash(Salt, Name, Seed) % All.size()];
	return Out;
}

}	// namespace

double NextPicks::Bpm() const
{
	return std::atof(Tempo.Value.c_str());
}

NextPicks NextPicksOf(int Kind, const std::string &TrackName, unsigned int Seed)
{
	const NextKind &K = NextKindInfo(Kind);
	NextPicks P;
	char Bpm[32];
	std::snprintf(Bpm, sizeof(Bpm), "%g", K.Bpm);
	P.Lead = PickOf(K.Leads, K.LeadKind, "lead", TrackName, Seed);
	P.ChordStyle = PickOf(K.ChordStyles, K.ChordStyle, "chords", TrackName, Seed);
	P.BassStyle = PickOf(K.BassStyles, K.BassStyle, "bass", TrackName, Seed);
	P.Groove = PickOf(K.Grooves, K.Groove, "groove", TrackName, Seed);
	P.Tempo = PickOf(K.Tempos, Bpm, "tempo", TrackName, Seed);
	P.Key = PickOf(K.KeyPool, K.Keys[0], "key", TrackName, Seed);
	return P;
}

std::string NextKindNames()
{
	std::string Out;
	for (int k = NEXT_CALM; k < NEXT_KIND_COUNT; ++k)
		Out += (k == NEXT_CALM ? "" : k + 1 == NEXT_KIND_COUNT ? " or " : ", ") + std::string(NextKindInfo(k).Name);
	return Out;
}

bool ParseNextKind(const std::string &Text, int &Kind)
{
	std::string Word;
	for (size_t i = 0; i < Text.size(); ++i)
		if (std::isalpha(static_cast<unsigned char>(Text[i])))
			Word += static_cast<char>(std::tolower(static_cast<unsigned char>(Text[i])));
	if (Word == "calm" || Word == "ambient" || Word == "night" || Word == "peaceful")
		Kind = NEXT_CALM;
	else if (Word == "lively" || Word == "stage")
		Kind = NEXT_LIVELY;
	else if (Word == "action" || Word == "battle" || Word == "boss" || Word == "fight")
		Kind = NEXT_ACTION;
	else {
		// The later kinds: their names and aliases
		for (int k = NEXT_COZY; k < NEXT_KIND_COUNT; ++k) {
			const NextKind &K = NextKindInfo(k);
			std::string Words = std::string(K.Name) + " " + (K.Aliases != 0 ? K.Aliases : "") + " ";
			if (Words.find(Word + " ") == 0 || Words.find(" " + Word + " ") != std::string::npos) {
				Kind = k;
				return true;
			}
		}
		return false;
	}
	return true;
}

bool KindWantsMajor(int Kind)
{
	return Kind == NEXT_CALM || NextKindInfo(Kind).KeyMode > 0;
}

bool KindWantsMinor(int Kind)
{
	return NextKindInfo(Kind).KeyMode < 0;
}

// Eight bars each, ending open (on V, or IV for a calm song), so a repeat, a break or the loop leads back in
std::string NextProgression(int Kind, bool Minor, bool B)
{
	if (Kind == NEXT_CALM) {
		if (Minor)
			return B ? "| VI | III | iv | i | VI | III | iv | VII |" : "| i | VI | iv | i | VI | iv | VII | i |";
		return B ? "| vi | IV | I | V | vi | IV | ii | IV |" : "| I | vi | IV | I | vi | IV | ii | IV |";
	}
	if (Kind == NEXT_ACTION) {
		if (Minor)
			return B ? "| iv | VI | VII | i | iv | VI | VII | V |" : "| i | VI | VII | i | i | VI | VII | V |";
		return B ? "| vi | IV | V | I | vi | IV | V | V |" : "| I | bVII | IV | I | I | bVII | IV | V |";
	}
	// A cozy song's: the friendly turnaround (I vi ii V) and plain steps home; B leans on IV and the relative minor
	if (Kind == NEXT_COZY && !Minor)
		return B ? "| IV | I | ii | V | IV | I | vi | V |" : "| I | vi | IV | V | I | vi | ii | V |";
	// The later kinds' own, where they have them
	const NextKind &K = NextKindInfo(Kind);
	const char *const *pOwn = Minor ? K.ProgMinor : K.ProgMajor;
	if (Kind > NEXT_COZY && pOwn[B ? 1 : 0] != 0)
		return pOwn[B ? 1 : 0];
	if (Minor)
		return B ? "| iv | VI | VII | III | iv | VI | VII | V |" : "| i | VI | III | VII | i | VI | iv | V |";
	return B ? "| IV | V | iii | vi | IV | V | ii | V |" : "| I | V | vi | IV | I | V | IV | V |";
}

// The example phrase

namespace {

// A note of a rhythm: its bar in a two-bar cell (0 or 1), its beat and how long it lasts
struct Hit {
	int Bar;
	double Beat, Beats;
};

// Two-bar cells in 4/4: an open one (it ends on a held note) and a closing one (a held note and a rest)
const Hit LIVELY_OPEN[] = { { 0, 1, 1 }, { 0, 2, 0.5 }, { 0, 2.5, 0.5 }, { 0, 3, 1.5 }, { 0, 4.5, 0.5 }, { 1, 1, 1.5 }, { 1, 2.5, 0.5 },
	{ 1, 3, 2 } };
const Hit LIVELY_CLOSE[] = { { 0, 1, 0.5 }, { 0, 1.5, 0.5 }, { 0, 2, 1 }, { 0, 3, 1 }, { 0, 4, 1 }, { 1, 1, 3 } };
const Hit SYNCOPATED_OPEN[] = { { 0, 1, 1.5 }, { 0, 2.5, 1 }, { 0, 3.5, 0.5 }, { 0, 4, 1 }, { 1, 1, 0.5 }, { 1, 1.5, 0.5 }, { 1, 2, 2 } };
const Hit SYNCOPATED_CLOSE[] = { { 0, 1, 1 }, { 0, 2, 1 }, { 0, 3, 0.5 }, { 0, 3.5, 0.5 }, { 0, 4, 1 }, { 1, 1, 2.5 } };
const Hit CALM_OPEN[] = { { 0, 1, 2 }, { 0, 3, 1 }, { 0, 4, 1 }, { 1, 1, 3 } };
const Hit CALM_CLOSE[] = { { 0, 1, 1 }, { 0, 2, 1 }, { 0, 3, 2 }, { 1, 1, 2 } };
const Hit CALM_OPEN_2[] = { { 0, 1, 1 }, { 0, 2, 2 }, { 0, 4, 1 }, { 1, 1, 2 }, { 1, 3, 1 } };
const Hit CALM_CLOSE_2[] = { { 0, 2, 1 }, { 0, 3, 1 }, { 0, 4, 1 }, { 1, 1, 3 } };

int Mod12(int Value)
{
	return ((Value % 12) + 12) % 12;
}

bool Has(const std::vector<int> &Classes, int PitchClass)
{
	return std::find(Classes.begin(), Classes.end(), Mod12(PitchClass)) != Classes.end();
}

}	// namespace

std::vector<TuneNote> ExampleTune(const Key &K, int FirstBar, int Bars, int BeatsPerBar, int RowsPerBeat,
	const std::vector<std::vector<int> > &Chord, const std::vector<std::vector<int> > &Sounding, bool Calm, int Variant)
{
	Bars = Bars >= 4 ? 4 : 2;
	BeatsPerBar = std::max(1, BeatsPerBar);
	RowsPerBeat = std::max(1, RowsPerBeat);
	// The rhythm: an open cell and a closing one for four bars, the closing one alone for two
	std::vector<Hit> Rhythm;
	auto AddCell = [&](const Hit *pHits, size_t Count, int Offset) {
		for (size_t i = 0; i < Count; ++i) {
			Hit H = pHits[i];
			H.Bar += Offset;
			Rhythm.push_back(H);
		}
	};
	if (BeatsPerBar == 4) {
		// Variant's bit 0 picks the open cell's rhythm, bit 1 the closing one's
		const bool OpenTwo = (Variant & 1) != 0, CloseTwo = (Variant & 2) != 0;
		const Hit *pOpen = Calm ? (OpenTwo ? CALM_OPEN_2 : CALM_OPEN) : (OpenTwo ? SYNCOPATED_OPEN : LIVELY_OPEN);
		const Hit *pClose = Calm ? (CloseTwo ? CALM_CLOSE_2 : CALM_CLOSE) : (CloseTwo ? SYNCOPATED_CLOSE : LIVELY_CLOSE);
		const size_t OpenCount = Calm ? (OpenTwo ? 5 : 4) : (OpenTwo ? 7 : 8);
		const size_t CloseCount = Calm ? 4 : 6;
		if (Bars == 4)
			AddCell(pOpen, OpenCount, 0);
		AddCell(pClose, CloseCount, Bars == 4 ? 2 : 0);
	}
	else {
		// Other meters: a note a beat (two a beat when fast and lively), the cell's last bar one held note and a rest
		for (int Cell = 0; Cell < Bars / 2; ++Cell) {
			for (int b = 1; b <= BeatsPerBar; ++b) {
				const Hit H = { 2 * Cell, static_cast<double>(b), 1.0 };
				Rhythm.push_back(H);
			}
			const Hit Held = { 2 * Cell + 1, 1.0, static_cast<double>(std::max(1, BeatsPerBar - 1)) };
			Rhythm.push_back(Held);
		}
	}

	// The pitches: the tonic as home, from G-3 to F#4 (as a pulse plays it), so the phrase sits over chords written before
	// it (a D major one around D-4, not D-3); it rises from its third to a high point past the middle and comes home, its
	// open cell ending on the fifth
	const int Home = 36 + Mod12(K.Tonic) + (Mod12(K.Tonic) < 7 ? 12 : 0), Low = Home - 5, High = Home + 12;
	const int Third = Home + ((K.Mode == MODE_MINOR || K.Mode == 1 || K.Mode == 2 || K.Mode == 6 || K.Mode == 7) ? 3 : 4);
	const int Rows = Bars * BeatsPerBar * RowsPerBeat;
	auto RowOf = [&](const Hit &H) {
		return static_cast<int>(std::floor(((H.Bar * BeatsPerBar) + (H.Beat - 1.0)) * RowsPerBeat + 0.5));
	};
	auto Length = [&](const Hit &H) { return std::max(1, static_cast<int>(std::floor(H.Beats * RowsPerBeat + 0.5))); };
	auto Rubs = [&](int Pitch, int From, int To) {
		for (int r = std::max(0, From); r < To && r < static_cast<int>(Sounding.size()); ++r)
			for (size_t i = 0; i < Sounding[static_cast<size_t>(r)].size(); ++i) {
				const int Apart = Mod12(Pitch - Sounding[static_cast<size_t>(r)][i]);
				if (Apart == 1 || Apart == 11)
					return true;
			}
		return false;
	};
	std::vector<TuneNote> Out;
	int Previous = Third;
	const size_t Count = Rhythm.size();
	for (size_t i = 0; i < Count; ++i) {
		const Hit &H = Rhythm[i];
		const int From = RowOf(H), To = std::min(Rows, From + Length(H));
		// Where the line heads: up from the third to a high point (the sixth above home) three fifths in, then home; the
		// open cell rests on the fifth, and the last note on home or the third
		const double Along = Count > 1 ? static_cast<double>(i) / static_cast<double>(Count - 1) : 1.0;
		double Target = Along < 0.6 ? Third + (Home + 9 - Third) * (Along / 0.6) : Home + 9 - 9.0 * (Along - 0.6) / 0.4;
		const bool OpenEnd = Bars == 4 && i + 1 < Count && Rhythm[i + 1].Bar >= 2 && H.Bar < 2;
		if (OpenEnd)
			Target = Home + 7;
		if (i + 1 == Count)
			Target = (Variant & 4) != 0 ? Third : Home;	// bit 2: it ends on the third
		const bool Strong = H.Beats >= 1.0 || std::fabs(H.Beat - 1.0) < 1e-9 || (BeatsPerBar == 4 && std::fabs(H.Beat - 3.0) < 1e-9);
		const std::vector<int> &Under = From < static_cast<int>(Chord.size()) ? Chord[static_cast<size_t>(From)] : std::vector<int>();
		int Best = -1;
		double BestCost = 0.0;
		for (int Pass = 0; Pass < 4 && Best < 0; ++Pass) {
			// Relaxed pass by pass: the chord's tones on strong notes, then any of the key's, then rubs allowed
			for (int p = Low; p <= High; ++p) {
				if (!InKey(p, K))
					continue;
				if ((Pass == 0 || Pass == 2) && Strong && !Under.empty() && !Has(Under, p))
					continue;
				if (Pass < 2 && Rubs(p, From, To))
					continue;
				// Near where the line heads; a step from the note before between strong notes, a step or a third onto
				// one; seldom the same note again; moving the way the line heads
				const int Move = p - Previous, Size = std::abs(Move);
				double Cost = 0.5 * std::fabs(p - Target);
				if (i > 0) {
					if (Strong)
						Cost += Size == 0 ? 2.0 : Size <= 2 ? 0.0 : Size <= 4 ? 0.5 : Size <= 7 ? 1.5 : 4.0;
					else
						Cost += Size == 0 ? 3.0 : Size <= 2 ? 0.0 : Size <= 4 ? 1.5 : 4.0;
					if (Size > 0 && (Target > Previous + 0.5) != (Move > 0) && std::fabs(Target - Previous) > 0.5)
						Cost += 0.75;
				}
				if (Best < 0 || Cost < BestCost - 1e-9) {
					Best = p;
					BestCost = Cost;
				}
			}
		}
		if (Best < 0)
			Best = Home;
		TuneNote N;
		N.Bar = FirstBar + H.Bar;
		N.Beat = H.Beat;
		N.Pitch = Best;
		N.Beats = H.Beats;
		Out.push_back(N);
		Previous = Best;
	}
	return Out;
}

}	// namespace ftapi

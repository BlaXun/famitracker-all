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

// The small set of tools (/mcp/core) as a local model sees it: each tool's own short description and arguments
// (ToolSet::CoreInfo), and its replies without the tools it doesn't have (ToolSet::CoreReply). The full set (/mcp)
// keeps its own texts; these are for a small context (the list was 27k tokens with the full set's texts, October 2026)

#include "Tools.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace ftapi {

namespace {

// An argument's text on the small set; Text 0 leaves the argument out of its schema (only arguments no call written on
// the small set uses: TestCoreSet and FollowWhatNext check the walks' calls, TestUnknownArguments the texts'), "" keeps it
// without a description, where its name and its choices say it
struct CoreArgText {
	const char *Name;		// "notes.pitch": a property of the items of an array argument
	const char *Text;
};

struct CoreToolText {
	const char *Name;
	const char *Description;
	std::vector<CoreArgText> Args;
};

// Arguments many tools share, unless a tool gives its own
const CoreArgText SHARED[] = {
	{ "from_bar", "First bar (from 1)." },
	{ "to_bar", "Last bar, included." },
	{ "section", "A section's name (get_song lists them), in place of from_bar and to_bar." },
	{ "channel", "Channel name (\"VRC6 Pulse 1\", \"Triangle\") or number." },
	{ "melody_channel", "The melody's channel (default: guessed)." },
	{ "key", "Key, like \"A minor\" (default: the track's)." },
	{ "instrument", "Instrument name from get_song, or a preset." },
	{ "volume", "Volume 1-15." },
	{ "chords", "Chords instead of fitting them: \"| Am7 | F | C | G7 |\", degrees \"I vi IV V\", or a list of bars." },
	{ "kind", "The song's kind; give the same every time." },
	{ "as_clip", 0 },
	{ "clip_name", 0 },
};

const std::vector<CoreToolText> &CoreTexts()
{
	static const std::vector<CoreToolText> TEXTS = {
		{ "get_song", "Overview of the open song: tempo, bars, channels and what they play, instruments, clips, sections, the key "
			"and its chords, and whether changes go in directly or arrive as clips. Start here.", {} },
		{ "what_next", "The one next step for the song, with its call ready to send and why. Work in a loop: call it, make the "
			"call, ask again, until it says done.", {
			{ "skip", "Steps to leave out, like [\"drums\"]: tempo, key, lead, chords, melody, form, drums, echo, polish, mix..." },
			{ "melody_channel", "The melody's channel (default: the designed lead's, else the busiest)." },
			{ "about", "Words about the place, for an empty song's form (and the kind)." },
			{ "like", "A studied track to be like: \"Mega Man 3 Needle Man\" (never its tune)." },
			{ "all", "Also list every step found now." },
			{ "format", 0 } } },
		{ "read_notes", "One channel's notes: bar, beat, pitch, length, instrument, volume, effects. format steps gives a melody "
			"as write_notes takes it, a bar a line. Up to 64 bars a call.", {
			{ "format", "notes (default) or steps." },
			{ "step", "With steps: eighth (default), sixteenth or quarter." } } },
		{ "write_notes", "Writes a part on one channel, replacing its notes in those bars. Easiest in steps: a token a step "
			"(an eighth), | between bars; a pitch starts a note, - holds it (across | too), . rests: \"D4 G4 A4 B4 - A4 G4 - | "
			"B4 - - - . . D5 -\" is two bars of 4/4. B4v12 gives a volume. Phrases of 2 or 4 bars breathe at their end. Drums on "
			"the noise channel by name (kick, snare, hihat). The reply warns of range, key and sound problems with the fix.", {
			{ "notes", "A line of steps (see above), or a list of {bar, beat, pitch, beats}." },
			{ "notes.bar", "Bar (from 1)." },
			{ "notes.beat", "Beat it starts on, from 1 (1.5: the \"and\" of 1)." },
			{ "notes.pitch", "C-4, C#4, Db4 (C-3 is middle C); on the noise: kick, snare, hihat, open hihat." },
			{ "notes.beats", "Length in beats." },
			{ "notes.volume", "1-15, kept for the notes after." },
			{ "notes.instrument", "This note's instrument, if not the part's." },
			{ "notes.effects", "Effects on its first row, like [\"445\"]." },
			{ "notes.later_effects", "Effects later in it: [{\"beats\": 1, \"effects\": [\"A02\"]}]." },
			{ "notes.end_effects", "Effects where it ends." },
			{ "notes.pan", "left, right, center, or -1 to 1; kept for the notes after." },
			{ "instrument", "Instrument name from get_song, or a preset (default: the channel's own there). Drums: one per hit, "
				"like {\"hihat\": \"Hat\", \"kick\": 3}." },
			{ "from_bar", "First bar to replace (default: the first note's; steps start there)." },
			{ "to_bar", "Last bar to replace (default: the last note's)." },
			{ "step", "What a step is: eighth (default), sixteenth or quarter." } } },
		{ "place_clip", "Places one of the song's clips (get_song lists them) at a bar.", {
			{ "clip", "The clip's name or number." },
			{ "channel", "Channel for its first column." },
			{ "bar", "Bar to place it at." },
			{ "beat", "Beat in the bar (default 1)." },
			{ "linked", 0 } } },
		{ "add_instrument", "Adds an instrument from a preset (the list below). Give the channel it's for: each chip needs its "
			"own. write_notes and set_instrument take preset names directly too.", {
			{ "preset", "The sound." },
			{ "channel", "The channel it's for (picks the chip)." },
			{ "chip", "The chip, without a channel (default 2A03)." } } },
		{ "design_instrument", "Makes an instrument from words when no preset fits: start from a kind and change what should "
			"differ (envelope, duty, vibrato, attack). Envelope times are in frames, 60 a second.", {
			{ "name", "Its name, like \"Hero lead\"." },
			{ "words", "The sound in words, like \"a soft pad with a gentle vibrato\" (what they name wins over the kind)." },
			{ "kind", "lead (default), pad, pluck, bass, keys, bell, brass, strings, organ, chiptune, swell; music box, breath, "
				"falling strike (calm leads); bloom lead, stepped lead, click pluck, echo pluck, thump bass; kick, snare, hihat, "
				"open hihat, crash, tom." },
			{ "channel", "The channel it's for (picks the chip)." },
			{ "chip", "" },
			{ "wave", "FDS: a wave by word (sine, hollow, voice...); N163: a wave sound's name." },
			{ "modulation", "FDS only." },
			{ "modulation_delay_beats", 0 },
			{ "attack", "instant, fast, medium, slow, swell, bloom, or frames 0-60." },
			{ "attack_from", 0 },
			{ "decay", "none, short, medium, long, or frames 0-120." },
			{ "decay_style", 0 },
			{ "sustain", "Level held: none (dies away), low, half, high, full, or 0-15. A tune's notes need one." },
			{ "release", "short, medium, long, or frames 0-60." },
			{ "restrike_beats", "Strike held notes again every so many beats (0: never)." },
			{ "echo", "A soft echo after each note." },
			{ "click", "A duty click on each note's first frame." },
			{ "volume", "Its loudest, 1-15." },
			{ "duty", "Pulse width: 12.5%, 25%, 50%, 75% (VRC6: steps of 6.25% to 50%)." },
			{ "duty_motion", "" },
			{ "vibrato", "" },
			{ "vibrato_delay_beats", 0 },
			{ "attack_pitch", "" },
			{ "arpeggio", "A chord as a fast arpeggio from each note." },
			{ "arpeggio_frames", 0 },
			{ "tone", 0 },
			{ "length", 0 },
			{ "noise", "5B only: its noise, alone or with the square." },
			{ "noise_pitch", 0 } } },
		{ "set_instrument", "Gives one channel's notes in some bars another instrument (the song's, or a preset).", {
			{ "instrument", "Instrument name or number, or a preset." } } },
		{ "set_tempo", "Sets the tempo: give bpm (it picks speed and tempo). With from_bar, a change inside the song; with "
			"over_bars, gradually. Needs direct changes.", {
			{ "bpm", "Beats per minute." },
			{ "speed", "FamiTracker's speed, 1-31 (lower is faster)." },
			{ "tempo", "FamiTracker's tempo, 32-255 (150 usual)." },
			{ "from_bar", "A change inside the song from this bar (default: the whole track)." },
			{ "beat", "With from_bar: the beat it starts on." },
			{ "to_bar", "With from_bar: the last bar at the new tempo." },
			{ "over_bars", "With from_bar: reach it gradually over this many bars." },
			{ "section", "A section at its own tempo." } } },
		{ "mix", "Sets how loud each part plays, keeping its dynamics. style calm or lively mixes as heard (each part against "
			"the melody); soft (default) the classic NES balance; full near full volume. Needs direct changes.", {
			{ "style", "soft (default), full, calm or lively." },
			{ "levels", "Loudest volume per channel, like {\"Noise\": 10}." },
			{ "dac", "DPCM level: full, warm, soft, softest, keep, or 0-127." },
			{ "width", "Stereo width (default keep)." },
			{ "faders", "keep (default) or reset: the mixer's faders back to 0 dB first." } } },
		{ "set_swing", "Makes the rhythm swing (speed changes every half beat). Needs direct changes; set it again after "
			"set_tempo changes the speed.", {
			{ "amount", "none, light, medium, hard, or a percentage 50-75." },
			{ "unit", "eighths (default) or sixteenths." },
			{ "by", 0 },
			{ "channels", 0 } } },
		{ "add_chords_and_bass", "Writes chords on a pulse and a bass on the triangle or sawtooth: your progression, or chords "
			"fitted to the melody. Pick chord_style, color and bass_style for the mood. Chords and bass already there are "
			"replaced.", {
			{ "chords", "The progression: \"| Am7 | Fmaj7 | C | G7sus4 G7 |\", degrees \"I vi IV V\", or a list of bars "
				"[\"Am7\", \"F G\"]. \".\" holds the chord before a share, \"%\" repeats a bar; it repeats to fill the bars." },
			{ "chord_channel", "Pulse channel for the chords, or \"none\"." },
			{ "chord_channels", "Voiced chords: 2-4 channels, a voice each." },
			{ "bass_channel", "Triangle or sawtooth for the bass, or \"none\"." },
			{ "color", "plain triads, sevenths, or rich (add9, maj7...)." },
			{ "chord_style", "arpeggio (NES shimmer), stabs (bouncy), broken (flowing), guide (quiet pad line), lush (ringing "
				"sevenths, soft), voices (a channel per note)." },
			{ "rhythm", "When arpeggio, stabs or lush chords are struck." },
			{ "pattern", "Order of a broken (or lush) chord's notes." },
			{ "notes_per_beat", 0 },
			{ "arp_frames", "Lush chords: frames a note, 1-6 (3-4 dreamier)." },
			{ "strum", 0 },
			{ "bass_style", "root_fifth (default), roots (calm), half, octaves (energetic), walking (jazz), syncopated, "
				"arpeggio (rolling), driving (action), pedal (tension)." },
			{ "half_bars", 0 },
			{ "chord_instrument", "Instrument or preset for the chords." },
			{ "chord_volume", "The chords' volume, 1-15." },
			{ "bass_instrument", "Instrument or preset for the bass (default \"bass\")." } } },
		{ "add_drums", "Writes a drum part on the noise channel: a groove with small changes, fills where phrases end and "
			"crashes. Instruments are added from presets.", {
			{ "groove", "basic (rock), four_on_floor, half_time, shuffle, march, breakbeat, calm, driving (action), waltz, "
				"lively (gentle, no snare)." },
			{ "intensity", "light, normal (default) or busy." },
			{ "fill_every", "A fill every so many bars (default 4; 0 none)." },
			{ "crash", "Crashes where the drums start and after fills (default true)." },
			{ "volume", "The loudest hit's volume, 1-15." },
			{ "kit", "noise (default) or samples: a sampled kick and snare on the DPCM channel, punchier." },
			{ "channel", "The noise channel (default: the song's)." } } },
		{ "add_ambience", "Adds a quiet layer of the place's sounds on the noise channel: waves, rain, wind, a stream or a "
			"fire, in step with the bars and the loop, under the music.", {
			{ "kind", "waves, rain, wind, stream or fire (default: from about or the track's name)." },
			{ "about", "Words about the place, for the kind." },
			{ "volume", "1-15 (default: about 9 dB under the melody)." },
			{ "channel", "The noise channel (default: the song's)." } } },
		{ "add_harmony", "Writes a second voice following the melody, a third under it by default, in the key and on the "
			"chords so it doesn't rub. Best on a channel like the melody's, quieter.", {
			{ "channel", "Channel for the harmony (default: a free one)." },
			{ "interval", "" },
			{ "long_notes_only", "Only under notes of a beat or more." },
			{ "volume", "Its volume, 1-15 (default 3 softer than the melody)." },
			{ "instrument", "Instrument or preset (default: the melody's)." } } },
		{ "grow_melody", "Grows a hook into a melody over the song's chords, in versions to choose from (phrases, a high point, "
			"chord tones on strong beats, breaths). Call it, then call again with the same arguments and pick to write one; "
			"then shape it by hand.", {
			{ "motif", "The hook: 4-7 notes in steps, a bar or two, like \"D4 G4 A4 B4 - A4 G4 -\"." },
			{ "channel", "The melody's channel." },
			{ "from_bar", "First bar." },
			{ "to_bar", "Last bar (up to 32 bars a call)." },
			{ "kind", "calm, lively or action: how it moves." },
			{ "variant", "1 (default), 2, 3...: other versions." },
			{ "pick", "Writes this version (1-3). Without it the versions are only shown." },
			{ "step", "What a step is: eighth (default), sixteenth or quarter." },
			{ "instrument", "With pick: the notes' instrument." } } },
		{ "polish", "Gives parts the small touches that make them sound played: accents, a delayed vibrato on long melody notes, "
			"gaps between repeated triangle notes, and nudges for clicking notes. Run it after the parts are written.", {
			{ "touches", "Default: accents, vibrato, separate, clicks. Also when asked: fades, scoops, falls, staccato, clashes "
				"(parts a semitone apart), colors (chord tones that rub the melody)." },
			{ "style", "any, calm or lively." },
			{ "channels", "A channel, a list, or \"all\" (default: every melodic one)." },
			{ "colors", "colors touch: fit (default) or add." } } },
		{ "add_echo", "Copies a channel's notes onto a free channel, later and quieter: the classic NES echo, spacious and "
			"dreamy. Echo notes that would rub are left out.", {
			{ "channel", "Channel to echo." },
			{ "to_channel", "Channel for the echo (default: a free one)." },
			{ "delay_beats", "How much later, in beats (default 3 rows)." },
			{ "volume", "1-15 at the original's loudness (default 6)." },
			{ "detune", "Fine pitch steps, -8 to 8 (1: a subtle chorus)." },
			{ "instrument", "Instrument for the echo (default: the original's)." },
			{ "pan", "\"opposite\" (the other side of the original), left, right, center, or -1 to 1." } } },
		{ "check_loop", "How the song loops: the intro, the loop and the seam (notes hanging over, settings that differ the "
			"second time). Run it before exporting.", {} },
		{ "fix_loop", "Fixes what check_loop finds at the seam, so the loop starts the way it does the first time. Run it "
			"after the last change.", {} },
		{ "set_loop", "Sets where the song loops back to (bars before it play once, as an intro), or with stop plays once and "
			"stops. Needs direct changes.", {
			{ "from_bar", "Where the loop starts (a frame's start; default 1)." },
			{ "to_bar", "The loop's last bar (default: the song's last)." },
			{ "section", "A section that loops, in place of the bars." },
			{ "stop", "Play once and stop after to_bar." },
			{ "stop_beat", 0 } } },
		{ "insert_bars", "Puts empty bars in at at_bar, in every channel; the rest moves later. Needs direct changes.", {
			{ "at_bar", "The new bars go before this bar." },
			{ "bars", "How many (default 1)." },
			{ "name", "A section name for the new bars." } } },
		{ "name_section", "Names bars as a part of the form (Intro, A, B, Break...), so tools take section in place of bars. "
			"Needs direct changes.", {
			{ "name", "The section's name, like \"Chorus\"." },
			{ "from_bar", "First bar." },
			{ "to_bar", "Last bar (default from_bar)." },
			{ "section", "A section to rename (with name) or clear." },
			{ "clear", "Take the name away." } } },
		{ "song_shape", "Lays out a song's form as named sections in frames: stage, night, town, boss, small or jingle, picked "
			"by the shape or words about the place, varied with add, drop, bars or parts. Without arguments it lists them.", {
			{ "shape", "stage, night, town, boss, small or jingle." },
			{ "about", "Words about the place." },
			{ "variant", "Which form, from 1." },
			{ "add", "Parts to put in: climax, break, bridge, outro, intro, b, a'." },
			{ "drop", "Parts to take out." },
			{ "bars", "About how long, 8-256 bars." },
			{ "parts", "The whole list: \"Intro 4, A 8, A' 8, B 8, Break 4\"." },
			{ "like", "A studied track: its loop's length." },
			{ "add_bars", "A track with notes: add the bars the form lacks." } } },
		{ "set_key", "Chooses the track's key, which every tool then uses. It moves no notes. Needs direct changes.", {
			{ "key", "Like \"D dorian\", \"A minor\", \"C major pentatonic\"; \"auto\" finds it from the notes." } } },
		{ "export_for_game", "Saves the track as a looping OGG file for a game, next to the song (the loop points in the reply). "
			"Run check_loop first; a song without a file is saved first.", {
			{ "tracks", "\"this\" (default), \"all\", or tracks by number or name." },
			{ "quality", 0 },
			{ "normalize", 0 },
			{ "loudness", "The loop's loudness in LUFS, like -20." },
			{ "attach", "Put the files in the reply too." } } },
		{ "finish_song", "The last steps in one call, once the parts are written: polish, mix, the loop's seam, a listen and "
			"the export, for the kind. The reply lists what it heard with the calls that fix it.", {
			{ "skip", "Steps to leave out: polish, faders, mix, loop, listen, export." },
			{ "faders", "auto (default), keep or reset." },
			{ "loudness", "The export's loudness in LUFS." },
			{ "quality", 0 } } },
		{ "listen", "Renders the song's real audio and says what an ear would notice (vibrato too deep, notes out of tune, "
			"clicks, a covered melody...), each with the call that fixes it. Changes nothing; slower than check_song.", {
			{ "parts", "Channels to hear alone (default: each)." },
			{ "melody_channel", "The melody (default: guessed), or \"none\"." },
			{ "format", "text (default), short or json." } } },
		{ "copy_bars", "Copies bars to another place (a section coming back), every channel by default. Vary the copy "
			"afterwards so it doesn't sound pasted.", {
			{ "from_bar", "First bar to copy." },
			{ "to_bar", "Last bar to copy (default from_bar)." },
			{ "section", "A section to copy, in place of the bars." },
			{ "at_bar", "Where the copy starts." },
			{ "times", "How many copies (default 1)." },
			{ "channels", "A channel, a list, or \"all\" (default)." },
			{ "semitones", "Move the copy up or down, -24 to 24." } } },
		{ "vary_part", "Changes a melodic part a little, for a repeat: embellish, simplify, syncopate, octave_up, octave_down, "
			"ending (to the tonic), restrike, rest (sits the bars out).", {
			{ "variation", "" },
			{ "rhythm", "restrike: the rhythm." } } },
		{ "reference_sound", "Adds a studied game's sound (find_sound lists them with this call) as an instrument with its "
			"exact envelopes.", {
			{ "reference", "The reference's id." },
			{ "sound", "The sound's number (default 1)." },
			{ "channel", "The channel it's for (picks the chip)." },
			{ "name", "The instrument's name." } } },
		{ "find_sound", "Searches every sound the song can get (presets, the sound library, studied games, the song's own), "
			"by words, kind or channel; each with the call that adds it and its warnings.", {
			{ "query", "Words: a kind (bell, pluck, bass, pad), a quality (warm, soft), a game." },
			{ "kind", "" },
			{ "from", "all (default), presets, references or song." },
			{ "channel", "Sounds that play on this channel." },
			{ "other_chips", "Also sounds of chips the song doesn't have." } } },
		{ "review_arrangement", "How plain the song sounds, as a musician hears it: parts missing, thin bars, plain chords, "
			"repeats, flat dynamics, length. Scores the depth out of 10 and gives the tips that matter most with their calls.", {
			{ "style", "any, calm or lively (default: calm when the track's name says so)." },
			{ "format", "short (default here) or text (every line)." } } },
		{ "check_song", "Checks the notes as written: notes out of key or range, clashing parts, clicks, notes that vanish, and "
			"what drafts miss. Each finding comes with the call that fixes it.", {
			{ "key", "Key to check against (default: the track's)." },
			{ "style", "any, calm or lively (default: calm when the track's name says so)." },
			{ "format", "short (default here), text or json." } } },
		{ "play", "Plays the song in FamiTracker for the user (from a bar, some channels, looping). listen is how you hear "
			"it; ask the user how it sounds.", {
			{ "from_bar", "Bar to start at (default 1)." },
			{ "bars", "How many bars (default: to the end)." },
			{ "section", "A section to play." },
			{ "loop", "Over and over until stop." },
			{ "channels", "Only these channels." },
			{ "mute", "Not these channels." } } },
		{ "stop", "Stops playback.", {} },
		{ "save_song", "Saves the song to its file, or a copy (copy_name). A song never saved goes into FamiTracker's Songs "
			"folder.", {
			{ "copy_name", "File name for a copy (default: save the song itself)." },
			{ "name", "A song with no file yet: its name." } } },
		{ "new_song", "Starts a new song in place of the open one, as File > New does; saved at once into the Songs folder "
			"under its name. Refuses while the open song has unsaved changes (save_song first). Then ask what_next.", {
			{ "name", "The song's name, like \"Town theme\"." },
			{ "expansion", "Expansion chips: VRC6, VRC6 Plus, VRC7, FDS, MMC5, N163, 5B, or none (default: the template's)." } } },
		{ "open_song", "Opens another song from the Songs folder or the recent files, by name; without a name, lists them. "
			"Refuses while the open song has unsaved changes.", {
			{ "name", "The song's name, like \"Town theme\"." },
			{ "list", "List the songs, opening none." },
			{ "path", "From this computer only: a .ftm file's whole path." } } },
		{ "set_expansion", "Sets the expansion chips, before writing: VRC6 (two pulses and a sawtooth), VRC7 (FM), MMC5, FDS, "
			"N163 (wavetables), 5B, VRC6 Plus (the VRC6 with a soft layer), or none. Channels of chips switched off are "
			"cleared: then it asks for confirm, which needs the user's OK. Can't be undone.", {
			{ "chips", "The chips, together." },
			{ "chip", "Or one chip alone, or none." },
			{ "n163_channels", "N163 channels, 1-8." },
			{ "confirm", "True once the user agreed to lose the notes on channels that go." } } },
		{ "create_backup", "Keeps a backup copy of the song as it is, labelled, before a batch of changes (restore_backup "
			"goes back to it). Needs a song with a file.", {
			{ "label", "Why, a few words: \"before the new intro\"." } } },
		{ "list_backups", "Lists the song's backups, newest first.", {
			{ "format", 0 } } },
		{ "restore_backup", "Opens a backup in the song's place (a backup of the song as it is is kept first). Tell the user "
			"which and why.", {
			{ "backup", "Its number or label." } } },
		{ "get_recipe", "The steps for composing a kind of song with these tools; without a kind, how to choose one.", {
			{ "kind", "The kind (default: how to choose)." },
			{ "like", "A studied track: its picks first." },
			{ "idea", 0 },
			{ "bars", 0 } } },
		{ "explain", "A music or tracker term in plain words, as FamiTracker's glossary has it (a chord, swing, an effect like "
			"4xy). Use these words with the user.", {
			{ "term", "The term, like \"chord\", \"duty\" or \"4xy\"." },
			{ "category", "Without a term: list a category's terms." } } },
		{ "write_log", "Writes a line into FamiTracker's agent log, where the user follows what you do: log each decision "
			"briefly, saying why. Changes nothing in the song.", {
			{ "text", "What you decided and why, a sentence or two." },
			{ "category", "idea, plan, research, fix, check, note (default) or error." } } },
		{ "read_user_notes", "Reads the notes the user wrote to you in the agent log since you last read them. Call it between "
			"steps and follow them.", {
			{ "all", "Every note, read or not." } } },
	};
	return TEXTS;
}

const CoreToolText *CoreTextOf(const std::string &Name)
{
	const std::vector<CoreToolText> &Texts = CoreTexts();
	for (size_t i = 0; i < Texts.size(); ++i)
		if (Name == Texts[i].Name)
			return &Texts[i];
	return 0;
}

// An argument's text: the tool's own, else the shared one; Found false when neither names it
const char *ArgText(const CoreToolText *pTool, const std::string &Name, bool &Found)
{
	Found = true;
	if (pTool != 0)
		for (size_t i = 0; i < pTool->Args.size(); ++i)
			if (Name == pTool->Args[i].Name)
				return pTool->Args[i].Text;
	for (size_t i = 0; i < sizeof(SHARED) / sizeof(SHARED[0]); ++i)
		if (Name == SHARED[i].Name)
			return SHARED[i].Text;
	Found = false;
	return 0;
}

// A description cut to its first sentence, for an argument neither table names
std::string FirstSentence(const std::string &Text)
{
	for (size_t i = 0; i + 1 < Text.size(); ++i)
		if (Text[i] == '.' && Text[i + 1] == ' ')
			return Text.substr(0, i + 1);
	return Text;
}

// A schema's properties with the small set's texts (Prefix "notes." for an array's items); the ones left out go from
// required too, which they never are
Json ShortSchema(const Json &Schema, const CoreToolText *pTool, const std::string &Prefix)
{
	Json Out = Json::MakeObject();
	for (size_t m = 0; m < Schema.Members().size(); ++m) {
		const std::string &Key = Schema.Members()[m].first;
		const Json &Value = Schema.Members()[m].second;
		if (Key != "properties" || !Value.IsObject()) {
			Out.Set(Key, Value);
			continue;
		}
		Json Properties = Json::MakeObject();
		for (size_t p = 0; p < Value.Members().size(); ++p) {
			const std::string &Name = Value.Members()[p].first;
			const Json &Property = Value.Members()[p].second;
			bool Found = false;
			const char *pText = ArgText(pTool, Prefix + Name, Found);
			if (Found && pText == 0)
				continue;
			Json Short = Json::MakeObject();
			for (size_t f = 0; f < Property.Members().size(); ++f) {
				const std::string &Field = Property.Members()[f].first;
				const Json &Data = Property.Members()[f].second;
				if (Field == "description" && Data.IsString()) {
					if (!Found || *pText != 0)	// "": the name and the choices say it
						Short.Set(Field, Found ? std::string(pText) : FirstSentence(Data.String()));
				}
				else if (Field == "items" && Data.IsObject() && Data.Get("properties") != 0)
					Short.Set(Field, ShortSchema(Data, pTool, Prefix + Name + "."));
				else
					Short.Set(Field, Data);
			}
			Properties.Set(Name, Short);
		}
		Out.Set(Key, Properties);
	}
	return Out;
}

bool IsWordChar(char c)
{
	return std::islower(static_cast<unsigned char>(c)) != 0 || std::isupper(static_cast<unsigned char>(c)) != 0 ||
		std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool StartsWith(const std::string &Text, size_t At, const char *pWhat)
{
	return Text.compare(At, std::strlen(pWhat), pWhat) == 0;
}

// Where Text names one of the tools Others at or after From: [Begin, End), false for none. A name with an underscore
// names its tool anywhere; a plain word (transpose, effects, references, undo) only where it reads as a call: before
// " {", " with " or " lists", or in brackets of its own, and transpose wherever it stands
bool FindMention(const std::string &Text, const std::vector<std::string> &Others, size_t From, size_t &Begin, size_t &End)
{
	bool Any = false;
	for (size_t i = 0; i < Others.size(); ++i) {
		const std::string &Name = Others[i];
		for (size_t At = Text.find(Name, From); At != std::string::npos; At = Text.find(Name, At + 1)) {
			const size_t After = At + Name.size();
			if ((At > 0 && IsWordChar(Text[At - 1])) || (After < Text.size() && IsWordChar(Text[After])))
				continue;
			const bool Mentioned = Name.find('_') != std::string::npos || Name == "transpose" ||
				StartsWith(Text, After, " {") || StartsWith(Text, After, " with ") || StartsWith(Text, After, " lists") ||
				(At > 0 && Text[At - 1] == '(' && After < Text.size() && (Text[After] == ')' || Text[After] == ','));
			if (!Mentioned)
				continue;
			if (!Any || At < Begin) {
				Begin = At;
				End = After;
				Any = true;
			}
			break;
		}
	}
	return Any;
}

// The bracket around At in Text (its "(" and past its ")"), false when At isn't in one on its line
bool BracketAround(const std::string &Text, size_t At, size_t &Open, size_t &Close)
{
	int Depth = 0;
	size_t i = At;
	while (i > 0) {
		--i;
		if (Text[i] == '\n')
			return false;
		if (Text[i] == ')')
			++Depth;
		else if (Text[i] == '(') {
			if (Depth == 0)
				break;
			--Depth;
		}
		if (i == 0)
			return false;
	}
	if (Text[i] != '(')
		return false;
	Open = i;
	Depth = 0;
	for (size_t j = At; j < Text.size() && Text[j] != '\n'; ++j) {
		if (Text[j] == '(')
			++Depth;
		else if (Text[j] == ')') {
			if (Depth == 0) {
				Close = j + 1;
				return true;
			}
			--Depth;
		}
	}
	return false;
}

// A line's sentences: each piece ends after ". " (not after a list's number, "1. ") or at the line's end
std::vector<std::string> Sentences(const std::string &Line)
{
	std::vector<std::string> Out;
	size_t Start = 0;
	// A heading of a word or two ("Note: ", "Why: ") is a piece of its own
	const size_t Colon = Line.find(": ");
	if (Colon != std::string::npos && Colon > 0 && Colon <= 12 && std::isupper(static_cast<unsigned char>(Line[0])) != 0) {
		bool Words = true;
		for (size_t c = 0; c < Colon; ++c)
			Words = Words && (std::isalpha(static_cast<unsigned char>(Line[c])) != 0 || Line[c] == ' ');
		if (Words) {
			Out.push_back(Line.substr(0, Colon + 2));
			Start = Colon + 2;
		}
	}
	for (size_t i = Start; i + 1 < Line.size(); ++i) {
		if ((Line[i] != '.' && Line[i] != '!' && Line[i] != '?') || Line[i + 1] != ' ')
			continue;
		size_t Word = i;
		while (Word > Start && Line[Word - 1] != ' ')
			--Word;
		bool Number = Word < i;
		for (size_t c = Word; c < i; ++c)
			Number = Number && std::isdigit(static_cast<unsigned char>(Line[c])) != 0;
		if (Number)
			continue;
		Out.push_back(Line.substr(Start, i + 2 - Start));
		Start = i + 2;
	}
	Out.push_back(Line.substr(Start));
	return Out;
}

// A sentence without the alternatives and brackets that name Others; empty when the sentence itself is about one
std::string WithoutMentions(std::string Sentence, const std::vector<std::string> &Others)
{
	size_t Begin = 0, End = 0;
	for (int Guard = 0; Guard < 32 && FindMention(Sentence, Others, 0, Begin, End); ++Guard) {
		// The tool with its arguments' words in a bracket after it ("add_counter_melody (a line of its own)")
		size_t Through = End;
		if (StartsWith(Sentence, Through, " (")) {
			size_t Open = 0, Close = 0;
			if (BracketAround(Sentence, Through + 2, Open, Close) && Open == Through + 1)
				Through = Close;
		}
		// An alternative: "X or Y", "Y, or X", "Y or X"
		if (StartsWith(Sentence, Through, " or ")) {
			Sentence.erase(Begin, Through + 4 - Begin);
			continue;
		}
		if (Begin >= 5 && Sentence.compare(Begin - 5, 5, ", or ") == 0) {
			Sentence.erase(Begin - 5, Through - (Begin - 5));
			continue;
		}
		if (Begin >= 4 && Sentence.compare(Begin - 4, 4, " or ") == 0) {
			Sentence.erase(Begin - 4, Through - (Begin - 4));
			continue;
		}
		// In a bracket of its own: the bracket goes
		size_t Open = 0, Close = 0;
		if (BracketAround(Sentence, Begin, Open, Close)) {
			const size_t From = Open > 0 && Sentence[Open - 1] == ' ' ? Open - 1 : Open;
			Sentence.erase(From, Close - From);
			continue;
		}
		return std::string();
	}
	return FindMention(Sentence, Others, 0, Begin, End) ? std::string() : Sentence;
}

// Whether what's left of a line says nothing: empty, a list's number, or a heading whose text went ("Note: review:")
bool SaysNothing(const std::string &Left, const std::string &Line)
{
	size_t Last = Left.find_last_not_of(" \r");
	if (Last == std::string::npos)
		return true;
	bool Number = true;
	for (size_t i = 0; i < Last; ++i)
		Number = Number && (std::isdigit(static_cast<unsigned char>(Left[i])) != 0 || Left[i] == ' ' || Left[i] == '-');
	if (Number && (Left[Last] == '.' || Left[Last] == '-'))
		return true;
	const size_t LineLast = Line.find_last_not_of(" \r");
	return Left[Last] == ':' && (LineLast == std::string::npos || Line[LineLast] != ':');
}

}	// namespace

ToolInfo ToolSet::CoreInfo(const ToolInfo &Full)
{
	ToolInfo Info = Full;
	const CoreToolText *pTool = CoreTextOf(Full.Name);
	if (pTool != 0)
		Info.Description = pTool->Description;
	Info.InputSchema = ShortSchema(Full.InputSchema, pTool, std::string());
	return Info;
}

bool ToolSet::HasCoreText(const std::string &Name)
{
	return CoreTextOf(Name) != 0;
}

bool ToolSet::NamesOtherTool(const std::string &Text, const std::vector<std::string> &Others)
{
	size_t Begin = 0, End = 0;
	return FindMention(Text, Others, 0, Begin, End);
}

std::string ToolSet::CoreReply(const std::string &Text, const std::vector<std::string> &Others)
{
	size_t Begin = 0, End = 0;
	if (!FindMention(Text, Others, 0, Begin, End))
		return Text;
	std::vector<std::string> Lines;
	size_t Start = 0;
	for (;;) {
		const size_t Newline = Text.find('\n', Start);
		const std::string Line = Text.substr(Start, Newline == std::string::npos ? std::string::npos : Newline - Start);
		if (!FindMention(Line, Others, 0, Begin, End))
			Lines.push_back(Line);
		else {
			std::string Kept;
			const std::vector<std::string> Pieces = Sentences(Line);
			for (size_t i = 0; i < Pieces.size(); ++i)
				Kept += WithoutMentions(Pieces[i], Others);
			while (!Kept.empty() && Kept[Kept.size() - 1] == ' ')
				Kept.erase(Kept.size() - 1);
			if (!SaysNothing(Kept, Line))
				Lines.push_back(Kept);
		}
		if (Newline == std::string::npos)
			break;
		Start = Newline + 1;
	}
	std::string Out;
	for (size_t i = 0; i < Lines.size(); ++i)
		Out += (i == 0 ? "" : "\n") + Lines[i];
	return Out;
}

}	// namespace ftapi

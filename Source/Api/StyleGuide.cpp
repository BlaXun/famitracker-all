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

// The built-in guide to the classic NES styles (References.h)

#include "References.h"
#include "Music.h"

#include <algorithm>
#include <cctype>

namespace ftapi {

namespace {

struct Section {
	const char *Topic;		// "konami"
	const char *Title;		// "Konami action (Bucky O'Hare, TMNT II)"
	const char *Words;		// more words that find it
	const char *Text;
};

// What the studies of Bucky O'Hare, TMNT II, Mega Man 2 to 5, Little Nemo,
// Super Mario Bros. 2 and 3 found (study_nsf's numbers), and studies of
// modern FamiTracker modules (the atmospheric section), as craft to write
// with. Every instrument recipe here is a design of this guide's, not a copy.
const Section SECTIONS[] = {
	{ "konami", "Konami action (Bucky O'Hare, TMNT II, Castlevania II)", "bucky ohare turtles tmnt castlevania action",
		"Feel: driving and dense; 112-167 BPM, often written with 8 rows a beat (rows of 3.5-4 frames) so attacks, flicks and "
		"echoes can sit between sixteenths. Mostly minor (11 of 14 Bucky tracks).\n"
		"- Leads that speak: almost every note has an articulation (blooms in 13 of 14 Bucky tracks, duty clicks in 11). "
		"Attacks bloom (9 or 11 to 15 in 2-3 frames), the first "
		"frame clicks at another duty (12.5% before a 25% or 50% body), long notes are struck again inside the envelope (a dip "
		"of 4-5 levels and a rise, every eighth), quick pitch flicks on the attack, decays in steps, and an echo built into "
		"the envelope (the note fades to 1-2, then comes back at 3-4 and fades). design_instrument kind \"bloom lead\" or "
		"attack bloom, click, restrike_beats 0.5, echo, attack_pitch flick.\n"
		"- The pulses play quietly (volume column 6-9 of 15) and close together: an echo of the lead a row behind (5 of 14 "
		"Bucky tracks), the two in rhythm on different notes (chords between them), or doubling in unison with different "
		"duties and volumes (12.5% at 9 with 75% at 5: thick, like a chorus).\n"
		"- Triangle: busy sixteenths, staccato (cut with Sxx after 3-4 frames), a pedal on the root with neighbour notes "
		"(0 0 +2 0 0 +4 0 0 ...), or a riff doubled with a pulse an octave up.\n"
		"- Drums on the DPCM: a short kick sample on the beats with pushes (x... xx.. x... xx..) and a longer snare sample "
		"either on 2 and 4 or on every 'and' (..x. ..x. ..x. ..x.), which doubles the drive; the noise adds a crack or a hat. "
		"add_sample_drums gives the DPCM channel a kit (write_notes plays kick and snare there by name); add_drums keeps the "
		"hats on the noise.\n"
		"- Harmony: long minor pedals (i for 4-8 bars) that then move by VI-III-VII; phrygian neighbours (i-bII-i); "
		"mixolydian vamps (I-bVII); sus4 chords; chords climbing by whole steps (bVI-bVII-I) into the loop's peak." },
	{ "capcom", "Capcom action (Mega Man 2-5, Little Nemo)", "megaman mega man rockman nemo ducktales capcom action",
		"Feel: fast and clear; 150-200 BPM at 4 rows a beat (speed 5 or 6 at tempo 150 is 180 or 150 BPM). Mega Man mostly "
		"minor (10 of 16 in MM2, 14 of 18 in MM3), Little Nemo mostly major.\n"
		"- Staccato everywhere: most notes are cut short (Sxx) or have short envelopes, so the fast lines stay clean.\n"
		"- The stepped lead: full volume held about 20 frames, then 3 lower held as long, then 3 lower again, with a short "
		"release; 50% or 25% duty (75% for a hollow edge). design_instrument kind \"stepped lead\". Leads at 12-13 of 15. "
		"From Mega Man 3 on, many leads bloom in 2-3 frames instead (12 of 18 MM3 tracks).\n"
		"- Second pulse: harmony in thirds struck with the lead (6 of 16 MM2 tracks), or an echo a row or two behind, or "
		"its own countermelody; arpeggio chords for the sustained harmony.\n"
		"- Triangle: the gallop x.xx (an eighth and two sixteenths) on one note per bar or half bar, octave pedals, and runs "
		"between; an occasional thump (the note starts a fourth up and drops in 3 frames: attack_pitch thump).\n"
		"- Noise drums: a snare on 2 and 4 (.... x... .... x...) with a 10-frame decay (15 15 11 11 11 7 7 7 3 0), and hats "
		"on most sixteenths (x.xx ..xx) with a 3-4 frame decay (15 5 5 0); crashes at section starts.\n"
		"- Harmony: i-VI-VII-I (minor to a major tonic at the cadence), i-bVII, dorian i-IV, and a riff repeated a semitone "
		"higher every 4 bars to build tension." },
	{ "nintendo", "Nintendo (Super Mario Bros. 2 and 3)", "mario smb kondo nintendo",
		"Feel: light, bouncy and witty; mostly major (13 of 23 SMB3 tracks), 112-150 BPM, short loops (4-16 bars) built "
		"from a catchy riff; many rests, so every part has air.\n"
		"- Plucky staccato: a duty click (12.5% for a frame, then 25%; in 15 of 23 SMB3 tracks) and a quick fall from 15 to "
		"a low hold (2-3), released "
		"at once: design_instrument kind \"click pluck\". Pulses play at about 10 of 15.\n"
		"- Harmony in thirds (8 of 23 SMB3 tracks) or sixths between the pulses, or both pulses doubling a riff; swells on "
		"held chords.\n"
		"- Bass: bouncing between root, octave and fifth (0 +12 -5 +7), latin and calypso figures (x..x ..x. x...), "
		"walking lines.\n"
		"- Drums: the noise as a light hat on the offbeats, a crash now and then; samples for timpani, kicks and congas.\n"
		"- Harmony: I-bVII and I-IV vamps, chromatic neighbours (I-bII-I), I-IV-bVI, and a major tonic turning minor (I to "
		"i) for a shadow." },
	{ "atmospheric", "Atmospheric chiptune (warm, lush and nostalgic, made in FamiTracker)",
		"atmospheric atmosphere lush warm jazzy jazz nostalgic dreamy chill lofi depth deep modern famitracker vrc6 vrc7 soft "
		"swing sevenths ninths",
		"Feel: deep, warm and nostalgic, the sound of modern FamiTracker music (often with the VRC6 or VRC7): jazz harmony, "
		"everything soft, every line echoed, the rhythm swung. 120-150 BPM is common, at speed 3-6 with 4 or 8 rows a beat.\n"
		"- A quiet mix: leads at volume 4-6 of 15 on a plain instrument (no envelope: the volume column shapes each phrase, "
		"with Axy fades on held notes), echoes at 2-3, chord arpeggios at 4-7, the triangle at full under them and the DPCM "
		"level raised (Z20 to Z7F) so the triangle and the noise soften too. mix (style soft) gets most of the way.\n"
		"- An echo under nearly every melodic line: the same notes a beat (4 rows) later on a free channel, same instrument and "
		"octave, at about 40% of the volume, sometimes a step out of tune (P7F or P81); with the VRC7's extra channels, three "
		"lines can each have one. add_echo with delay_beats 1, volume 6 and detune 1.\n"
		"- Lush chord instruments: a library of arpeggio instruments, one per chord shape, looping their notes two frames "
		"each: maj7 0 4 7 11, m7 0 3 7 10, a ninth and eleventh on top (0 3 7 10 14 17), leaping (0 7 2 11), falling (11 7 4 0), "
		"spread over two octaves (0 7 14 22), sometimes three or four frames a note for a slower shimmer. They play on the "
		"pulses in octaves 2-3 at volume 4-7, struck again on syncopated rows (beats 2 and 4, or 3+3+2). add_chords_and_bass "
		"with chord_style lush (pattern alberti, down or rolling; arp_frames 3) makes them.\n"
		"- Harmony: sevenths and ninths almost everywhere (maj7 on I and IV, m7 or m9 on ii, iii and vi, 7 and 7sus4 on V), "
		"progressions that sidestep (IVmaj7 V iii7 vi7; ii9 V7sus4 Imaj7), passing m7b5, diminished and augmented chords. "
		"make_groove with the nostalgic mood starts there.\n"
		"- Swing: the speed alternating every row or half beat (6 and 3, 4 and 2, 4 and 3, 10 and 6), and repeating patterns "
		"like 3 3 3 4 for speeds between whole numbers. set_swing. Or only some parts swing: their notes on the second "
		"sixteenth of each pair late by G01-G03, a shuffle over a straight bass (set_swing by delays).\n"
		"- Expression: a vibrato that waits a beat (453-464, then 400 to stop it), slides into notes (Qxy, 2xx), sighs of a "
		"semitone or two where phrases end (R41, R42), late notes with G01-G03, quick octave blips on attacks (an arpeggio "
		"12 0; on the triangle bass, the thump bass preset), held notes fading (A0y), and on a plain instrument the duty "
		"changed note by note (V00 V01 V02 in turn) so runs shimmer; a staccato triangle, cut after a row or two. polish with "
		"touches fades, scoops, falls and staccato adds most of them.\n"
		"- Colors: VRC6 bells and pianos with long decays (14 13 12 11 10 8 6 5 4 3 3 3) and pulses at 25-50% for leads; on "
		"the VRC7, soft electric pianos of its own (custom patches) comp the chords across two or three channels, rolled a "
		"frame apart (Gxx), while its built-in sounds take leads and bass (chord_style voices).\n"
		"- Drums: short and long noise hats (14 2 0; 12 10 8 7 7 6 5...), snares and kicks whose noise pitch moves, and a "
		"sampled kick and snare on the DPCM channel with the level written on each hit (Zxx) so it stays where the mix put it "
		"(add_drums with kit samples, then mix)." },
	{ "leads", "Lead sounds and articulations", "lead melody sound instrument envelope articulation duty click bloom vibrato",
		"A lead sounds alive when each note has an attack, a body and an end that aren't flat:\n"
		"- Duty click: the first frame at a thin 12.5% (or 50% for a thin body) before the body's duty: notes speak, "
		"especially fast ones. design_instrument click.\n"
		"- Bloom: start a third below full and reach it in 2 frames (10 13 15): softer than a hard attack, punchier than a "
		"swell. attack bloom.\n"
		"- Restrike: a held note dips 4-5 levels and rises every eighth, so long notes keep the beat. restrike_beats 0.5.\n"
		"- Stepped decay: hold, then drop 3 levels at a time: punchy and clear at fast tempos. decay_style steps.\n"
		"- Echo tail: after the note fades, a soft repeat at a quarter of the volume. echo.\n"
		"- Pitch flick: up and back in 3 frames on the attack. attack_pitch flick. Scoop: up into the note from below.\n"
		"- Vibrato that waits: none for the first 12-24 frames, then 1-2 period steps either way over 6-8 frames per "
		"side; only long notes get it. vibrato gentle with vibrato_delay_beats. On the 2A03's pulses a vibrato that takes "
		"the period across a multiple of 256 restarts the wave, a click every time (A-3, period 253, with any vibrato): the "
		"tools nudge such notes a few cents with Pxx as they write them.\n"
		"- Swell: 8 to 15 over 6-30 frames for held, singing notes, with vibrato.\n"
		"reference_sound brings in a studied track's lead exactly; design_instrument makes your own." },
	{ "echo", "Echoes, doubling and the second pulse", "echo delay doubling unison second pulse harmony thirds chorus",
		"With two pulses, the second one is what makes a track sound big:\n"
		"- Echo: the lead's notes again on the other pulse a row or two later (an eighth at most), at about half the "
		"volume (4-7), often a thinner duty. add_echo does it. Konami's echo runs one row (6-8 frames) behind.\n"
		"- Doubling: both pulses in unison with different duties and volumes (12.5% loud with 75% soft): a thick, chorused "
		"lead. Or an octave apart.\n"
		"- Harmony: the second pulse a third or sixth below, struck with the lead (add_harmony); change to parallel sixths "
		"for the B part.\n"
		"- Chords between them: both pulses in the same rhythm on different chord tones, under a triangle melody.\n"
		"- Traded notes (hocket): one plays while the other rests, a row apart, for fast riffs that sound faster than one "
		"channel could." },
	{ "bass", "Triangle bass figures", "bass triangle gallop pedal octave walking thump",
		"The triangle has no volume, so its rhythm and note lengths do the work:\n"
		"- Gallop: x.xx (an eighth, two sixteenths) on one note, changing with the chord (Capcom's drive).\n"
		"- Sixteenth pedal with neighbours: 0 0 +2 0 0 +4 0 0, staccato (Konami's churn).\n"
		"- Octave and fifth bounce: 0 +12 0 +7 +9 0 +12 0 (rock), or 0 +12 -5 +7 (Nintendo's bounce).\n"
		"- Walking eighths through the chord with passing notes, for towns and jazz.\n"
		"- Staccato: cut notes after 3-6 frames (S03-S06) so the bass punches instead of droning.\n"
		"- Thump: start a fourth up and drop in 3 frames on the downbeats (attack_pitch thump, or kind \"thump bass\").\n"
		"add_chords_and_bass's bass_style and make_groove give these figures; write_notes for your own." },
	{ "drums", "Drums: noise kits, samples and grooves", "drums noise snare hat kick dpcm samples groove backbeat fills",
		"- Noise kit: hat 15 5 5 0 or 15 11 7 3 0 at a high noise pitch; snare 15 15 11 11 11 7 7 7 3 0 at a middle one; "
		"kick at a low pitch, short, its pitch falling; crash: a long fade at a high pitch. design_instrument kinds make "
		"these.\n"
		"- Grooves found: the backbeat (.... x... .... x...) under sixteenth hats (x.xx ..xx); the double-time drive (a "
		"kick on the beats, a snare on every 'and'); half-time with the snare on 3 for heavy parts; hats only on the "
		"offbeats for light ones.\n"
		"- Samples (DPCM): Konami and Nintendo use a short kick and a longer snare on the DPCM channel, with the noise "
		"adding hats; they sound fuller than noise alone. add_sample_drums adds a kit made for this (kick, snare, two toms), "
		"which write_notes plays by name on the DPCM channel; samples that start from silence keep the triangle loud.\n"
		"- Fills every 4 or 8 bars: sixteenth snares, or the hats dropping out for a bar before a section. add_drums adds "
		"fills." },
	{ "harmony", "Harmony and color", "harmony chords progression key mode minor major modal dorian phrygian mixolydian",
		"What the studied tracks do with chords (write your own progressions in these colors):\n"
		"- Long pedals: the tonic held 4-8 bars under a moving melody and bass, then a move that feels big (VI-III-VII, "
		"or a semitone up).\n"
		"- Minor with a major ending: i-VI-VII-I, the last chord major for a heroic lift.\n"
		"- Aeolian and mixolydian vamps: i-bVII, I-bVII, VI-VII-i: adventure without a dominant.\n"
		"- Dorian: i-IV with a major IV (brighter minor); phrygian: i-bII (danger, mystery).\n"
		"- Sus4 chords that resolve or don't, and chromatic shifts: a whole section moved up a semitone or step for the "
		"next loop, or every four bars to build tension.\n"
		"- Chords change once or twice a bar; the bass often anticipates them by an eighth." },
	{ "form", "Form, tempo and loops", "form loop intro tempo structure length sections",
		"- Tempo: 112.5, 128.6, 150 and 180 BPM are the common ones (speeds 8, 7, 6 and 5 at tempo 150, every row the same "
		"length); fast action 150-200, towns and maps 100-130. A tempo between them makes rows of two lengths, and hi-hats and "
		"arpeggios wobble: set_tempo names the even tempos nearby.\n"
		"- Loops: 16-32 bars for stages (4-bar phrases: A B A' C, A A' B B'), 4-12 for short loops (menus, bosses), with an "
		"intro of 1-2 bars (a drum fill or a riff) that doesn't come back.\n"
		"- Each loop pass should have a climax: the highest note, the busiest drums, a key change or the second pulse "
		"joining in harmony.\n"
		"- The loop point sits on a bar line; the last bar leads back (a fill, a pickup, the dominant)." },
	{ "texture", "Sharing four channels", "texture channels arrangement layers parts roles",
		"- Roles: lead (a pulse), second line (the other pulse: echo, harmony, chords or counter-melody), bass (triangle), "
		"drums (noise, samples). The roles can swap for a section: the triangle takes a melody while the pulses play "
		"chords.\n"
		"- Registers: the lead mostly in octave 3 (C-3 to C-4) reaching into octave 4 at its high points, the second pulse a "
		"third to an octave below or above it, the triangle's bass one or two "
		"octaves under the lead.\n"
		"- Dynamics come from the volume column and from which channels play: drop the second pulse for a verse, bring it "
		"back in harmony for the climax.\n"
		"- Leave room: rests in the lead let the echo and drums speak; busy bass under a sparse lead, or the other way "
		"round." },
	{ "soundtrack", "A game's soundtrack (many tracks, one world)",
		"soundtrack game ost score tracks places world town dungeon boss credits title ending leitmotif motif theme mmo rpg "
		"platformer catalogue loudness",
		"What composing a 30-track soundtrack for one game with these tools showed:\n"
		"- Plan the set first: a catalogue of places (title, the town by day and by night, home, shops, fields, caves, "
		"dungeons, bosses, events, credits), each with a mood, a chip, a key and a tempo, so that neighbours differ. One house "
		"sound (the VRC6) with a few places on another chip for their color: the N163 for magical calm (a reef, crystals, the "
		"stars), the VRC7 for a cozy home, the 2A03 alone for grit (a chase, the final boss).\n"
		"- A leitmotif: a motif of four to seven notes stated by the title track and brought back where the story needs it, "
		"changed for the scene: a dance at a festival, broken and in minor at the final boss, whole and in major when the "
		"fight turns, hinted under the stars, in full in the credits. A town can have a motif of its own that other tracks "
		"quote.\n"
		"- Tempos with even rows: speeds 6, 7, 8, 9, 10 and 11 at tempo 150 give 150, 128.6, 112.5, 100, 90 and 81.8 BPM; "
		"speed 7 with tempo 191 gives even eighths at 163.7 (a boss); in 6/8 (set_meter), speed 9 is a calm lilt of 66.7 "
		"dotted quarters. Tracks that share a tempo should differ in feel.\n"
		"- Loudness by place, so the tracks sit together in the game: export_for_game with loudness -22 LUFS for places to "
		"rest, -19 for action and bosses, -20 for the rest. Every track opens with an intro of 2-4 bars that plays once, 4-9 dB "
		"under the loop, and breathes before the loop comes round (a break of 2-4 bars).\n"
		"- Forms that work: a town or a field as A A' B A'' (the climax) and a break; a dungeon whose B climbs (a step a bar, "
		"or up in key) and falls back for the loop; a boss with a short, tense intro (set_meter bars_per_frame 2 makes 2-bar "
		"frames, so a 2-bar intro loops cleanly) and a 6-bar climax; a final boss as two tracks, one per phase, the "
		"second made from the first's material, so the game switches when the boss changes; the credits playing once and "
		"ending (set_tempo with from_bar and over_bars slows it down a step a beat; set_loop with stop halts it after a last "
		"chord rings out).\n"
		"- Techniques for places: a hocket for clockwork (two pulses trading one tune note by note); gears of different sizes "
		"(an ostinato of three sixteenths against 4/4, lining up again at the bar line); a musette for a night ferry (two "
		"pulses in unison, one detuned P81, beating like an accordion's reeds) over bellows chords (an arpeggio instrument that "
		"swells and settles once a bar); drips and stars as sparse high notes at irregular places, with their own echo; waves "
		"and wind as noise swells (design_instrument kind swell on the noise channel); fireworks as a pulse whistle sliding "
		"up, a noise boom and falling bell sparks; call and response between the lead and bells, a horn or sound effects in "
		"its gaps; a signature sound for a place (a dungeon's hour bell, a workshop's finishing bell, a ship's bell rung in "
		"pairs).\n"
		"- Keep each track's notes in soundtrack_notes: where it plays, what you hear, its references and what each gave, "
		"its techniques, checks and loop points." },
};

const size_t SECTION_COUNT = sizeof(SECTIONS) / sizeof(SECTIONS[0]);

std::vector<std::string> SplitWords(const std::string &Text)
{
	std::vector<std::string> Out;
	std::string Word;
	for (size_t i = 0; i <= Text.size(); ++i) {
		const unsigned char c = i < Text.size() ? static_cast<unsigned char>(Text[i]) : ' ';
		if (std::isalnum(c))
			Word += static_cast<char>(std::tolower(c));
		else if (!Word.empty()) {
			Out.push_back(Word);
			Word.clear();
		}
	}
	return Out;
}

}	// namespace

std::vector<std::string> StyleGuideTopics()
{
	std::vector<std::string> Out;
	for (size_t i = 0; i < SECTION_COUNT; ++i)
		Out.push_back(SECTIONS[i].Topic);
	return Out;
}

std::string StyleGuide(const std::string &Topic)
{
	const std::vector<std::string> Asked = SplitWords(Topic);
	if (Asked.empty()) {
		std::string Out = "The guide to the classic NES styles and the atmospheric sound of modern FamiTracker music, from "
			"studies of their soundtracks. Topics (references action guide with a topic):\n";
		for (size_t i = 0; i < SECTION_COUNT; ++i)
			Out += std::string("- ") + SECTIONS[i].Topic + ": " + SECTIONS[i].Title + "\n";
		return Out;
	}
	// The sections whose topic, title or words hold the words asked for, best first
	std::vector<std::pair<int, size_t> > Found;
	for (size_t i = 0; i < SECTION_COUNT; ++i) {
		const std::vector<std::string> Own = SplitWords(std::string(SECTIONS[i].Topic) + " " + SECTIONS[i].Title + " " + SECTIONS[i].Words);
		int Score = 0;
		for (size_t a = 0; a < Asked.size(); ++a)
			for (size_t o = 0; o < Own.size(); ++o)
				if (Own[o] == Asked[a] || (Asked[a].size() > 3 && Own[o].compare(0, Asked[a].size(), Asked[a]) == 0)) {
					Score += Own[o] == SplitWords(SECTIONS[i].Topic)[0] ? 3 : 1;
					break;
				}
		if (Score > 0)
			Found.push_back(std::make_pair(-Score, i));
	}
	if (Found.empty())
		return "\"" + Topic + "\" isn't in the guide. " + StyleGuide(std::string());
	std::sort(Found.begin(), Found.end());
	std::string Out;
	for (size_t i = 0; i < Found.size() && i < 2; ++i) {
		const Section &S = SECTIONS[Found[i].second];
		Out += (i ? "\n" : "") + std::string(S.Title) + "\n" + S.Text + "\n";
	}
	return Out;
}

}	// namespace ftapi

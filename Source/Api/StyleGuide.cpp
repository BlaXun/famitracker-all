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
// with; and the studies of Castlevania, Castlevania III (the Japanese
// version, Akumajou Densetsu, with the VRC6: its sawtooth plays the bass
// and the triangle never sounds), Super C, The Legend of Zelda,
// Kid Icarus (the Famicom Disk System original), Gyromite, Punch-Out!!,
// Dr. Mario, Final Fantasy III, Journey to Silius, Strider and Nintendo World
// Cup (October 2026): the sections from castlevania to technos, and the dark
// and melancholy sections, which draw on all of them. The FDS's wave and
// modulation and Journey to Silius's DPCM bass are from nsf2ftm's conversion
// of those tracks, since a study keeps neither. Every instrument recipe here
// is a design of this guide's, not a copy.
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
	{ "castlevania", "Konami's gothic action (Castlevania, Castlevania III, Super C)",
		"castlevania akumajou densetsu dracula vampire gothic horror castle belmont konami super contra probotector "
		"military",
		"Feel: gothic and driven; 112.5-200 BPM at 4 rows a beat (129, speed 7, the commonest; 200 for Super C's late "
		"stages and bosses and three of Castlevania III's stages), almost always minor (8 of 10 Castlevania tracks, 17 of "
		"23 in Castlevania III, every Super C stage). The Castlevania III studied is the Japanese version, Akumajou "
		"Densetsu, with the VRC6: two more pulses and a sawtooth, which plays the bass while the triangle never sounds.\n"
		"- Quiet pulses over a loud bass: Castlevania's lead and second part mostly at volume 4-6 over the triangle, which "
		"has no volume and carries the weight; Castlevania III's sawtooth bass mostly at 12 (of 15) under a VRC6 lead at "
		"7-9, its echo or second part at 4-5, and 2A03 pulses at 4-6 whose echoes drop to 2-3. mix style soft, then the "
		"pulses' levels lower still.\n"
		"- Seven voices with the VRC6: the lead on a VRC6 pulse in most of Castlevania III's tracks (17 of 23 by the "
		"studies' count), the other VRC6 pulse its echo or a second part, the sawtooth the bass (on the password screen "
		"the tune, over a bass on the VRC6 pulses), the 2A03 pulses inner lines, chords between them and the quietest "
		"echoes, the noise and DPCM the drums; the triangle stays silent.\n"
		"- Duty by mood in Castlevania: 50% and 75% for the stages, 12.5% for the dark pieces (Dracula's quarters, a boss, "
		"the final battle). Super C plays 25% with a duty click (a 12.5% or 50% first frame); Castlevania III's VRC6 "
		"pulses mostly 31% (VRC6 duty 4; then 44%, 37.5% and 19%), a note's first frame most often at 6.25% (40 of their "
		"97 sounds), its 2A03 pulses 12.5% or 25%: duty clicks in 30 of the two games' 33 tracks. Envelopes fall from 15 "
		"to 8-9 in about 3 frames, hold at 6-11, then ring on at 2-6 for up to 30 frames after the note: design_instrument "
		"click, decay short, sustain low, release long.\n"
		"- The bass: Castlevania's and Super C's triangle, staccato in most tracks (Sxx on a quarter to all of its notes): "
		"a root pedal in eighths (x.x. x.x.), the leading tone rocking against the tonic for a cave. Castlevania III's "
		"sawtooth, in octaves 0-2: plucked (a first frame at 5, then 15 falling to 3-6 within 7-11 frames), octave churn "
		"in sixteenths for the final battles (+0 +12 over and over, 78% octave jumps), the tonic on every beat under "
		"Dracula's room, off-beat steps down a semitone and back (+0, then +8 +7 +6 +7 on the 'and's).\n"
		"- Weight from doubling: the triangle in unison or octaves with a pulse; Super C puts a pulse in octaves 1-3 at "
		"12.5-25% duty under the triangle in 7 of 10 tracks, a buzzing growl beneath the riff; Castlevania III doubles a "
		"line with the sawtooth (3 tracks) or puts a VRC6 pulse on a 2A03 pulse's line (3).\n"
		"- Echoes in places, two beats (8 rows) behind in Castlevania; in 21 of Castlevania III's 23 tracks, most often an "
		"eighth (2 rows) behind, 1-5 rows (8 in places on the password screen): one VRC6 pulse echoing the other at about "
		"half its volume (4 under 7-9), or a 2A03 pulse at 2-5 echoing a VRC6 part (3 under 9 in the final battles).\n"
		"- Drums: Castlevania's noise kit (a snare 15 15 15 5 5 5 3 3 3 0 whose noise pitch drops at frame 7, a hat 15 6 2 "
		"0); Castlevania III and Super C add DPCM: a tiny low thump as the kick (65 bytes in Castlevania III, 17 in Super "
		"C; up to 8 hits a bar in the final battles), 513-705-byte hits as snare and toms, and a cymbal held 6 frames on "
		"every eighth (final battles) or every sixteenth. add_drums with kit samples and groove driving.\n"
		"- Harmony: minor pedals of 5-8 bars; i alternating with VI or VII; the minor tonic alternating with the "
		"diminished chord on the same root; minor chords on bIII and bII (an alien lair); a chord a tritone away; sus4 "
		"chords climbing a semitone a bar into the loop; a boss's last phase the first one a semitone higher and faster "
		"(131 to 150 BPM: make_version, then transpose semitones 1 and set_tempo)." },
	{ "early-nintendo", "Nintendo's early sound (Zelda, Kid Icarus on the FDS, Gyromite, Punch-Out!!, Dr. Mario)",
		"zelda kondo tanaka hip kid icarus palutena fds famicom disk gyromite robot punch out punchout dr mario puzzle "
		"early",
		"Feel: clear and economical; Zelda and Kid Icarus at 112.5 BPM (speed 8, or 8 rows a beat of 4 frames) or 150-180, "
		"Dr. Mario 103-180 and mostly major (10 of 12); only Dr. Mario uses samples.\n"
		"- One blended sound: Zelda gives both pulses 50% duty and the same soft envelope, a swell from 9-12 to 15 over 4 "
		"frames (never a hard 15), with a slow tremolo written into long notes (15 13 13 13 13 11 11 11 11 9 9 9 9 9 9 11 "
		"13 13 15, round again about every 20 frames) and a vibrato after 15 frames on an 8-frame cycle. design_instrument "
		"attack 4, attack_from 9, vibrato gentle with vibrato_delay_beats 0.5.\n"
		"- Harmony in sixths between the pulses (Zelda) or thirds (Kid Icarus, 65-72% of the time); hats as one-frame "
		"ticks (15 0) on the bass's own rhythm.\n"
		"- Kid Icarus on the FDS: the FDS takes the lead only in the title and the ending, an octave above the pulses (G-4 "
		"to A#5): a sawtooth-ramp wave, its volume swelling 5 to 32 over 9 frames (or reaching 32 in 5 and fading over "
		"67), the modulator a gentle vibrato (speed 34, depth 5, a triangle-shaped table), while the pulses under it throb "
		"in a looping tremolo (8 9 11 13 15 13 11 9, every 8 frames). The tools can't make FDS instruments: the user draws "
		"one in FamiTracker's FDS editor.\n"
		"- Echoes: Kid Icarus's 3-4 rows behind at half the volume; Gyromite's at volume 2 under a lead at 8 (a shadow), "
		"or the triangle repeating the lead two beats later an octave down; Gyromite also plays a whole lead a few frames "
		"late (Gxx on every note), behind the beat.\n"
		"- Punch-Out!!'s struck tones: pulses that never hold (15 14 12 11 11 9 9 8 ... down over 22-34 frames) with the "
		"triangle doubling the lead, and a cymbal fading over 41 frames that wobbles a step as it goes.\n"
		"- Dr. Mario: 3-frame plucks (15 12 1 at 75% duty), a swung bass on a pulse in octave 2 (60% of its notes late by "
		"Gxx and cut short), hocket between the pulses, a 113-byte and a 241-byte sample as kick and snare under noise "
		"hats on three of every four sixteenths.\n"
		"- Zelda's dungeon and its final dungeon are models for dark places: topic dark." },
	{ "square", "Square's RPG sound (Final Fantasy III)",
		"square final fantasy ff3 uematsu rpg jrpg town field world map shrine crystal tower airship chocobo prelude",
		"Feel: lyrical and orchestral, a singing lead over a moving inner line and a triangle bass; 100-192 BPM (120 and "
		"150-190 the commonest), 27 of 47 tracks major. Square's rows aren't whole frames (4.7 to 8.9): take the nearest "
		"even tempo (set_tempo).\n"
		"- Drumless places: only 5 of 47 tracks use the noise channel (the battles, a fanfare, an ending). Towns, fields, "
		"caves, shrines and towers keep time with the triangle and a sixteenth figure on the second pulse; add_drums only "
		"for battles.\n"
		"- A loud lead and a soft inner line: the lead at 12 (25% duty, or 50% where it swells), the second pulse at 8-9 "
		"at 12.5% in running sixteenths (broken chords, octave flickers, scale steps), echoes at 6.\n"
		"- Envelopes that breathe: swells from 3-5 to 15 over 12-14 frames (23 of 47 tracks); a hold that wobbles a step "
		"every two frames (15 14 14 15 15 14 14 ...); a breath (15, a dip to 5, back up over 8 frames); fades of 30-61 "
		"frames with a vibrato that waits 16-50 frames (a 12-frame cycle, 4-7 cents). design_instrument attack 12, "
		"attack_from 4, release long, vibrato gentle with vibrato_delay_beats 0.75.\n"
		"- The triangle as the melody, a hollow voice for water and sorrow: a soft 50% pulse echoes it 3 rows later at "
		"volume 5 (add_echo from the triangle, to_channel a pulse, instrument soft lead), or it arpeggiates over two "
		"octaves under held pulse notes.\n"
		"- A lilt from delays: 20-63% of a part's notes a few frames late (Gxx) in 18 parts (set_swing by delays). Echoes "
		"in 11 tracks, mostly 1-4 rows behind at half the volume or a little more.\n"
		"- Harmony: bass lines stepping down (laments), i-VII-VI-V cadences, the dominant held as a pedal, minor triads "
		"sliding down a semitone at a time, chords a major third apart, the major and minor of one chord alternating, the "
		"Neapolitan in minor; for danger, chords a tritone or a semitone apart alternating." },
	{ "sunsoft", "Sunsoft's sound (Journey to Silius)", "sunsoft silius raf world kodaka sampled bass dpcm bass sci fi",
		"Feel: dark sci-fi drive; 112.5-200 BPM, mostly 150-200; 6 of 8 tracks minor, the other two chromatic; long tonic "
		"stretches broken by bII and diminished chords, chords a major third or a tritone apart.\n"
		"- The sampled bass: the bass line is on the DPCM channel, 1 KB bass samples played at rates 9-15 (12 keys in one "
		"stage alone), so it growls like a real instrument and leaves the pulses and the triangle free. In FamiTracker "
		"that's one sample on many keys at different pitches (an instrument's DPCM tab) or Wxx; the tools here make drum "
		"samples only (add_sample_drums), so the user loads a bass sample. A study calls such samples low thumps: "
		"import_nsf's DPCM keys tell.\n"
		"- The triangle as the kick: up to a third of its notes start 2 semitones up and drop in 2 frames, and a longer "
		"drop of 8 semitones over 8 frames is the bass drum (design_instrument kind thump bass, or attack_pitch drop on "
		"the triangle).\n"
		"- Echoes in 4 of 8: the lead again 3-5 rows later on the other pulse at volume 3-5 under 7-10 (add_echo "
		"delay_beats 0.75-1.25, volume 5).\n"
		"- Blooms from 8 to 15 in one frame on 25% and 12.5% pulses (attack bloom); scoops into notes from a semitone or "
		"two (attack_pitch scoop); for story scenes a swell from 2 to 15 over 10 frames that steps down and echoes, fading "
		"over 58.\n"
		"- Drums: a 6-frame snare (15 12 9 5 2 0) on syncopated grids (..x. x.x.), a cymbal held 1-2 frames on most "
		"sixteenths (10-16 a bar): a hissing, mechanical hat line.\n"
		"- Registers turned over in the prologue: both pulses in octaves 1-2 under the triangle, grave and heavy." },
	{ "strider", "Capcom's Strider (stealthy, staccato, 75% duty)",
		"strider hiryu capcom stealth spy infiltration base staccato nervous",
		"Feel: nervous and mechanical; 112.5 BPM (often 8 rows a beat of 4 frames) or 150, 11 of 14 tracks minor.\n"
		"- 75% duty for both pulses (21 of 28 parts): hollow and nasal, where Mega Man uses 25-50%.\n"
		"- Staccato everywhere: Sxx on a third to nearly all of the pulses' notes and up to 99% of the triangle's; sounds "
		"held at 15 and cut, with long releases (65-79 frames) only where a note is let go.\n"
		"- Echoes 1-3 rows behind at the same volume, the lead itself a few frames late on half its notes (Gxx).\n"
		"- A flutter: a pitch flick down on the attack, again every 6-7 frames, on a third of the lead's notes "
		"(design_instrument attack_pitch flick).\n"
		"- Hats on nearly every sixteenth (11-13 a bar, a 4-frame decay), no samples; the triangle in root-fifth-octave "
		"figures (+0 +7 +12 +7) under staccato sixteenths.\n"
		"- Harmony: the tonic and the minor chord a semitone above alternating every bar for 18 bars; bVI, bII and the "
		"major chord a tritone away in minor; a boss on one chord whose motion is a semitone grind in octaves 1-2, the "
		"second pulse and the triangle on the same two notes." },
	{ "technos", "Technos (Nintendo World Cup)",
		"technos kunio nekketsu nintendo world cup soccer football sports dodgeball",
		"Feel: fast and sporty; 141-176 BPM (one slow piece at 101), 6 of 9 tracks minor. Technos's rows are fractions of "
		"a frame (5.1 to 8.9): take the nearest even tempo.\n"
		"- 50% duty for nearly every part (14 of 18), the pulses quiet (volume 5), attacks that bloom from 12 to 15 in 2 "
		"frames and die in 5 (design_instrument attack bloom, sustain none): punchy and clipped.\n"
		"- One pulse runs broken-chord sixteenths under a lead in eighths; the triangle staccato on 47-100% of its notes, "
		"in root, fifth and third figures.\n"
		"- Hats on most sixteenths (10-11 a bar, x.xx ..xx, a 3-frame decay), a 10-frame snare on 2 and 4; noise only.\n"
		"- Harmony: chords falling by fifths through the key and turning on a chromatic dominant, for momentum; "
		"i-VII-VI-V; for the slow, wistful piece, modal mixture (bVI, the minor iv, the tonic turned minor).\n"
		"- An echo a row behind at another duty (a 50% echo of a 75% part), and the triangle trading notes with a pulse." },
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
	{ "dark", "Dark and claustrophobic (dungeons, caves, crypts, the underworld)",
		"dark dungeon dungeons cave caves cavern crypt tomb catacomb underworld underground labyrinth sewer mine ruins "
		"haunted eerie ominous creepy spooky horror claustrophobic menace dread lair",
		"What the dark tracks of Castlevania, Castlevania III, Super C, Zelda, Kid Icarus, Final Fantasy III, Journey to "
		"Silius and Strider share (write your own lines in these ways):\n"
		"- Register squeezed low and narrow: lines that stay inside a fifth or sixth (sixteenths by step around C-3), "
		"pulses in octaves 1-2 at 12.5-25% duty buzzing under or with the triangle, or the registers turned over with the "
		"triangle above the pulses. change_mood moves -12; keep the lead under C-4.\n"
		"- Thin and quiet: 12.5% duty where the stages use 50%; the lead at 4-6 with the triangle on top.\n"
		"- Semitones and tritones: two chords a tritone apart alternating, a bar or two each (15 bars of it), the tonic "
		"and the chord a semitone above alternating for 18-20 bars, the minor tonic and the diminished chord on the same "
		"root, minor chords on bIII and bII, minor triads sliding down a semitone a bar, chords stepping down "
		"chromatically over their bass. add_chords_and_bass takes them by name; make_groove dungeon starts dark.\n"
		"- A bass ostinato on a semitone: the leading tone rocking against the tonic in eighths, off-beat steps down a "
		"semitone and back (+0, then +8 +7 +6 +7 on the 'and's), a grind between two notes a semitone apart in octaves "
		"1-2; or a triangle ringing a minor third high up on the beats.\n"
		"- Pedals: the bass on the tonic every beat for dozens of bars under moving chords (43 bars of Castlevania III's "
		"sawtooth, 2 frames behind the beat); one minor chord for 8-12 bars; the dominant held 6 bars.\n"
		"- An ostinato that never stops: two pulses interlocking one broken chord, one on the beats and one on the 'and's; "
		"or a quiet sixteenth carpet on one pulse at volume 3 under slower parts (Castlevania III's Dracula's room: a 2A03 "
		"pulse at 50% duty under VRC6 pulses at 9).\n"
		"- No drums, or nearly none: Zelda's dungeons, Final Fantasy III's caves, shrines and towers (drums in 5 of its 47 "
		"tracks), 6 of Castlevania III's 23; the bass and the ostinato keep time. Where drums play, sparse and stumbling: "
		"a syncopated snare, 1-2 hats a bar, a long low noise once in a while.\n"
		"- Space and quiet echoes: a bass on the bar's first two beats only, or half a bar and then silence; a lead in "
		"half notes; echoes 2-4 rows behind at volume 2-3 (add_echo volume 3).\n"
		"- Tempo: 112.5 BPM (speed 8, or 8 rows a beat of 4 frames), or a fast grid (180) under slow parts. Loops that "
		"don't settle: 29 or 21 bars (four-bar phrases and one bar more), sus4 chords climbing a semitone a bar, a boss's "
		"second phase a semitone higher and faster.\n"
		"- Sounds: the triangle with a late vibrato (after 15 frames, about 15 cents) so the bass wavers; notes that fall "
		"2 semitones after 4 frames, or leap a tritone at their end (Castlevania III's boss); a pitch flutter on the "
		"attack.\n"
		"- Expansion chips: Castlevania III's VRC6 version gives the bass to the sawtooth in octaves 0-2 and never sounds "
		"the triangle; in its dark pieces the sawtooth holds a low pedal in notes of a bar or more that swell in, strikes "
		"the tonic on every beat or doubles a pulse's line, the VRC6 pulses carry most leads (mostly 31-44% duty, often "
		"after a 6.25% first frame) and their echoes at about half the volume, and the 2A03 pulses take inner lines, "
		"carpets and the quietest echoes (2-3), or a low tune a VRC6 pulse doubles. On the FDS, Kid Icarus's lead: a "
		"sawtooth-ramp wave, a slow modulator vibrato (speed about 34, depth 5), a 5-9-frame swell and a 67-frame fade." },
	{ "melancholy", "Melancholic and sad (laments, night, loss, endings)",
		"melancholy melancholic sad sorrow sorrowful tragic tragedy loss grief lament requiem mournful lonely night "
		"nocturne farewell funeral memorial wistful bittersweet rain goodbye",
		"What the sad and night pieces of Castlevania, Castlevania III, Final Fantasy III and Nintendo World Cup do:\n"
		"- A bass that steps down from the tonic (a lament) to the dominant, held for a few bars (Castlevania III's ending "
		"plays it on the sawtooth, a note a bar, twice); or the minor i-VII-VI-V again and again; a major last chord (the "
		"tonic or the dominant) for a glimmer.\n"
		"- No drums; the bass in long notes (the triangle a beat or two, Castlevania III's sawtooth a bar), or syncopated "
		"(half its notes off the beat), some a few frames late (Gxx).\n"
		"- Swells: 5 to 15 over 14 frames on a 50% pulse (Final Fantasy III), 3 to 15 over 26 frames on the sawtooth bass "
		"of Castlevania III's Nightmare; its ending swells most notes in over 3-5 frames (from 2-6) into long fades (35 "
		"frames on the 2A03 pulses, 88 on the sawtooth). design_instrument kind pad or lead, attack 4-26, attack_from 3-5, "
		"release long.\n"
		"- A vibrato that waits 20-65 frames (a 10-14-frame cycle; 4-6 cents in Final Fantasy III, 10-55 in Castlevania "
		"III) and fades of 30-90 frames on held notes (polish touches fades).\n"
		"- The echo close behind: a row (Final Fantasy III's maiden), or 2-4 rows at volume 2-6.\n"
		"- The triangle singing the melody, its notes bending down over 21 frames (a sigh), a soft pulse echoing it.\n"
		"- A tolling melody: Castlevania III's password screen gives its slow tune to the sawtooth in octave 3, a note a "
		"beat or two, each struck twice inside the note (15 9 13 13 9: a bell), echoed 3 rows behind at volume 2 by a 75% "
		"pulse, over a bass in octaves on both VRC6 pulses at 10.\n"
		"- Tempo: 108-128 BPM, or a fast grid (180) under long notes; 112.5 for the laments (rows of 8 frames, or 8 rows a "
		"beat of 4).\n"
		"- Night: the lead at 5 with an echo at 2 an eighth behind, the bass a low pedal in notes of a bar or more that "
		"swell in, no drums (Castlevania III's Nightmare); make_version night does the rest.\n"
		"- Wistful rather than tragic: modal mixture in a major key (bVI, the minor iv), the major and minor of one chord "
		"alternating." },
	{ "calm", "Calm songs (a place to rest, a night scene, a lullaby: when one is asked for)",
		"calm peaceful relaxing relaxed rest resting lullaby sleep sleepy quiet gentle serene tranquil soothing still "
		"peace restful",
		"What makes a song calm rather than plain or pressing, from the calm pieces of Final Fantasy III, Castlevania III and "
		"Punch-Out!!, modern FamiTracker music and a soundtrack whose listener loved the result. Only for songs asked to be "
		"calm: lively stages and fights have topics of their own (lively, action).\n"
		"- The melody's sound never holds a high, steady level: a level held from attack to release presses like an organ key "
		"held down, however soft the parts around it. Give the lead one of three shapes and keep it for the song: a breath "
		"(design_instrument kind breath: in from about 4 over 12 frames, settling to about 9, a faint vibrato after half a "
		"beat; Final Fantasy III's towns), a falling strike (kind falling strike: struck at 15, falling to 5-7 over 12-16 "
		"frames; its Time Remains) or a music box (kind music box: struck, fading to silence in about a beat; Punch-Out!!'s "
		"calm lead falls 15 to 0 over 34 frames). On the VRC6, duty 37.5% softens it further (Castlevania III's night lead). The "
		"reply gives the volume sequence and when it is down to half and silent.\n"
		"- The melody only a little over the chords: 2-3 dB as heard. mix style calm sets it, counting how each sound's "
		"envelope sounds over its notes; measure_mix style calm checks it on stems. The calm towns' leads play at 6-9.\n"
		"- Few notes: 2-4 a bar, mostly quarters, halves and wholes; phrases of two bars, then a rest of a beat or more; "
		"often a bar of two notes and a lot of air (x... .... .... x...). Steps and repeated notes (a third of Punch-Out!!'s "
		"repeat the one before); a leap of a third or fourth at most once a phrase, never onto a strong beat as its loudest "
		"note; one gentle high point a section, reached by step.\n"
		"- No accents, scoops, falls or flicks: polish style calm (a faint vibrato that waits, separate, clicks); no pitch "
		"slides on the lead (1xx 2xx 3xx Qxx Rxx). Laid back: a few chosen notes a frame or two late (Gxx 01-02), never early.\n"
		"- Slow harmony with color: a chord a bar or two (m7, maj7, add9, sus2, a sus4 that resolves slowly); plagal turns "
		"(iv-i, IV-I) and the minor iv in a major key instead of V-I pushes; the loop's last bar open (a sus2, a maj7). Lush "
		"arpeggio chords at volume 4-6 (add_chords_and_bass chord_style lush, arp_frames 3-4 for a slow shimmer) or held "
		"voices (chord_style voices, on the VRC6 Plus's soft layer), under the melody.\n"
		"- No drums: at most, in one section, a soft tick on the noise (volume 2-4) or a slow low thump.\n"
		"- A soft bass: the triangle can't be turned down, so few long notes with rests (a note a bar, or beats 1 and 3), "
		"kept out of the intro and the break; or the sawtooth swelling in (4 to 15 in 5 frames, then falling to 5 over 90: "
		"Castlevania III's Nightmare); or the VRC6 Plus's sub bass.\n"
		"- Slow, even tempos: 64.3 BPM (speed 14), 69.2 (13), 75 (12), 81.8 (11) at tempo 150, 4 rows a beat; rows of 10-14 "
		"frames give swells and fades room.\n"
		"- Depth underneath, so calm isn't thin: an echo a beat behind at about 40%, a step out of tune, on the other side "
		"(add_echo delay_beats 1, detune 1, pan opposite; on the VRC6 Plus onto a soft channel with the soft flute); an intro "
		"of the chords alone; a break that drops the melody; a pad or choir under the chords (on the VRC6 Plus also the "
		"pulsing pad, a slow breathing bed, or the quick pad for a chord every beat); the mixer's Soft and Space "
		"(set_mixer room hall, small room or cathedral for the place; space 15-30 on pads, 30-50 on the lead in a big place, "
		"more on glints and events; soft 20-50 on a pulse that should sound warm; the bass dry).\n"
		"- Checks: review_arrangement style calm (its tips that the melody doesn't stand out or has few, long notes are "
		"expected here), check_song style calm (no held lead, 0 clicking, 0 vanishing), measure_mix style calm with "
		"melody_channel the lead (the melody 2-3 dB over the chords, the intro 6-10 dB under the main part, the sides within 2 "
		"dB). export_for_game at -22 LUFS." },
	{ "lively", "Lively stages (bouncy and driving, never harsh)",
		"lively stage stages level levels platformer explore exploring bouncy bounce upbeat cheerful adventure jump run "
		"forest meadow day playful",
		"A stage to explore can bounce and drive, but its melody shouldn't press. What made earlier tracks sound aggressive "
		"was a lead that held 11-15 from attack to release, 5-8 dB over the chords; Mega Man 2's Bubble Man cuts 78% of its "
		"lead's notes short, with a stepped decay (15, then 13, then a fade over 8 frames). For a stage:\n"
		"- Notes that fade: design_instrument kind stepped lead (holds, then drops a few levels at a time), falling strike, or "
		"pluck with sustain half (settles to about 9); a duty click or a bloom so the notes speak. No sound that holds a high "
		"level for the whole note.\n"
		"- The melody 3-4 dB over the parts around it: mix style lively; measure_mix style lively checks it.\n"
		"- A motif a player would hum: a recognizable rhythm, developed; phrases of 2 or 4 bars that end on a held note or a "
		"rest; chord tones on strong beats; one high point a section; long and short notes mixed; mostly octave 3, reaching "
		"into octave 4 at the high points.\n"
		"- Harmony with color: sevenths, add9 and sus chords; bVI and bVII for a lift, the major IV in minor; a chord a bar, "
		"other chords for the B part.\n"
		"- Soft drums: add_drums groove lively (noise hats at volume 4-7 on the eighths, an open hat now and then, a soft "
		"kick, no snare backbeat), fills only where phrases end, counted from where the loop starts. The lively mix keeps "
		"drums as written (10-12 dB under the melody); it never raises them.\n"
		"- A melody that breathes: about 2 of every 3 two-bar spans end on a rest of a beat or a note held two beats "
		"(review_arrangement counts them); a tune that runs on sounds all over the place.\n"
		"- A bass that bounces (polish touch staccato): root and fifth, walking, or an octave figure; or the sawtooth.\n"
		"- polish touches vibrato, separate, clicks and staccato (no accents, scoops or falls on the lead).\n"
		"- Depth: an echo a beat behind on the other side, a counter-melody or harmony only in the repeat, the VRC6 Plus's "
		"soft layer for the B part, a little Space (15-30) on chords and the echo.\n"
		"- Tempo 112-160 BPM, at an even one (150 is speed 6, 128.6 speed 7, 112.5 speed 8, at tempo 150), straight or a light "
		"swing (set_swing with a percentage, 56-60).\n"
		"- Checks: review_arrangement 8 or more (tips asking for a louder melody or accents go against this: leave them), "
		"check_song 0 clicking and 0 vanishing, measure_mix style lively with melody_channel the lead. export_for_game at "
		"-20 LUFS." },
	{ "action", "Action: battles, bosses and chases (punchy and driving)",
		"action battle battles boss bosses fight fighting chase combat danger tense tension arena climax escape final",
		"Fights and chases keep the punch of the classic soundtracks: the melody on top, a backbeat, accents.\n"
		"- A lead with life that stands out: the singing lead, brass or strings presets, design_instrument kind bloom lead or "
		"stepped lead, a duty click; scoops into long notes after leaps and falls at phrase ends where they fit (polish "
		"touches scoops and falls, on chosen bars).\n"
		"- Drums that drive: a sampled kick and snare under noise hats (add_drums with kit samples, or make_groove), fills "
		"where phrases end, busier toward a climax.\n"
		"- A bass that drives: octaves, a gallop, a thump bass (bass_instrument \"thump bass\").\n"
		"- Harmony with tension: minor, phrygian or harmonic minor, bVI-bVII-i, a sus4 climbing a semitone a bar; the B part "
		"higher or in another key. The guide's konami, capcom and castlevania topics have the classic figures.\n"
		"- polish with its default touches (accents, vibrato, separate, clicks), then mix style soft (the default) or full, "
		"last.\n"
		"- Tempo 140-190 BPM, at an even one (150 is speed 6; speed 7 with tempo 191 gives even eighths at 163.7).\n"
		"- Checks: review_arrangement 8 or more, check_song 0 clicking and 0 vanishing, measure_mix with melody_channel the "
		"lead for the intro, the sections and the sides. export_for_game at -19 LUFS." },
	{ "leads", "Lead sounds and articulations","lead melody sound instrument envelope articulation duty click bloom vibrato",
		"A lead sounds alive when each note has an attack, a body and an end that aren't flat:\n"
		"- Duty click: the first frame at a thin 12.5% (or 50% for a thin body) before the body's duty: notes speak, "
		"especially fast ones. design_instrument click.\n"
		"- Bloom: start a third below full and reach it in 2 frames (10 13 15): softer than a hard attack, punchier than a "
		"swell. attack bloom.\n"
		"- Restrike: a held note dips over two frames to about half its level and is struck again, every eighth with "
		"restrike_beats 0.5, so long notes keep the beat; a pluck or a music box is struck again from its peak.\n"
		"- Stepped decay: hold, then drop 3 levels at a time: punchy and clear at fast tempos. decay_style steps.\n"
		"- Echo tail: after the note fades, a soft repeat at a quarter of the volume. echo.\n"
		"- Pitch flick: up and back in 3 frames on the attack. attack_pitch flick. Scoop: up into the note from below.\n"
		"- Vibrato that waits: none for the first 12-24 frames, then 1-2 period steps either way over 6-8 frames per "
		"side; only long notes get it. vibrato gentle with vibrato_delay_beats. On the 2A03's pulses a vibrato that takes "
		"the period across a multiple of 256 restarts the wave, a click every time (A-3, period 253, with any vibrato): the "
		"tools nudge such notes a few cents with Pxx as they write them.\n"
		"- Swell: 8 to 15 over 6-30 frames for held, singing notes, with vibrato.\n"
		"- Shapes that don't press, for calm songs and lively stages: a level held from attack to release presses like an "
		"organ key held down. A breath (in from about 4 over 12 frames, settling to about 60% of its peak), a falling strike "
		"(struck, falling to 5-7 over 12-16 frames) or a music box (struck, fading to silence in about a beat) doesn't: "
		"design_instrument kinds breath, falling strike and music box. A faint vibrato (3-7 cents, after half a beat) is "
		"vibrato faint.\n"
		"- A sound that lasts: a tune's quick notes need an envelope that holds or settles to a level (15 down to 9). One "
		"that's gone in 3-4 frames (15 10 5 2 1 0) turns them into blips, and beside held notes of the same line they sound "
		"like missing notes. A staccato tune settles low (the click pluck holds at 2-3) rather than to silence; sounds that "
		"vanish are for echoes, stabs and drums.\n"
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
	{ "vrc6-plus", "VRC6 Plus: the VRC6 with a soft layer",
		"vrc6 plus vrc6plus soft layer pad pads choir strings sub bass softer warmer deeper depth softness n163 wave",
		"VRC6 Plus is this FamiTracker's own chip: the real VRC6 (two pulses and the sawtooth) with a soft layer of four N163 "
		"wave channels, Soft 1 to Soft 4. It plays only soft waves, through a real chip's emulation, so it still sounds like "
		"the NES: a VRC6 track with a bit more to it.\n"
		"- The VRC6 carries the music: the melody and its answers on the pulses (25% or 50% duty for warmth, 12.5% to cut), "
		"the sawtooth's bass and bite, the triangle and the noise as ever. Write the track as a VRC6 track first.\n"
		"- The soft layer goes under it, quieter and optional: a pad or a choir holding the chords, a voice a channel "
		"(add_chords_and_bass chord_style voices, with the string pad or the choir pad); a soft echo of the melody a beat later "
		"(add_echo onto Soft 1 with the soft flute, volume 4-6); a sub bass doubling the sawtooth's bass an octave down (the sub "
		"bass, felt more than heard); or a long note that swells under a phrase's end.\n"
		"- Keep it under the VRC6: the pulses and the sawtooth at 12-15, the soft layer's parts 3-6 dB lower (mix keeps them "
		"there), a pad's voices at about 8-10. The starter pads swell in over 20-24 frames, which blurs quick changes: a chord "
		"a bar or half a bar.\n"
		"- Four more soft sounds come by name (add_instrument on a soft channel): breath (in over 11 frames, sinking to 10, a "
		"faint vibrato: a soft lead or a doubling), quick pad (in within 7 frames, for a chord every beat), pulsing pad (struck "
		"again every 12 frames, a soft echo after its release: a slow, breathing bed) and music box (a tine gone in 40 "
		"frames: glints). At volume 15 against a 2A03 pulse at 15 they play: choir pad 8.3 dB under, string pad 7.3, sub bass "
		"5.1, soft flute 5.3, breath 7.4, quick pad 7.7, pulsing pad 10.4, music box 4.8. A song holds at most 8 soft-layer "
		"sounds. design_instrument works on a soft channel too: a built-in sound's waves with the envelope asked for.\n"
		"- Use it for contrast: leave it out of a busy A part and bring the pad in for the B part, a bridge, a calm or night "
		"version; the choir pad for a sacred or eerie place, the string pad for warmth, the sub bass under a boss or a cave.\n"
		"- The four channels share the N163's one output, each heard a quarter of the time, which FamiTracker makes up; with "
		"four its switching whine stays above hearing.\n"
		"- More depth: View > Mixer has Soft (a gentle filter) and Space (a reverb and echo send) for each channel, which suit "
		"the soft layer and an echo (FamiTracker's own: its playback and the game's OGG files).\n"
		"- Leave the soft layer empty and the song is plain VRC6; set_expansion with chips [\"VRC6\"] goes back to it "
		"(asking before the soft channels' notes are cleared). NSF export plays one expansion chip, so a VRC6 Plus song goes "
		"to the game as OGG files (export_for_game)." },
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
		"rest, -19 for action and bosses, -20 for the rest. Every track opens with an intro of 2-4 bars that plays once, 6-10 dB "
		"under the loop's main part (13 dB under reads as silence in a game), and breathes before the loop comes round (a break "
		"of 2-4 bars). measure_mix checks it, with each section's loudness and the two sides (within 2 dB of each other).\n"
		"- Three kinds of song, each with a recipe in this guide: calm (only when a song is asked to be calm: a place to rest, a "
		"night scene), lively (a stage to explore) and action (fights, bosses, chases). The listener of a first set of 48 "
		"tracks found every melody too direct: a lead held at one level, 5-8 dB over the chords. Calm songs and lively stages "
		"answer that; action keeps its punch.\n"
		"- What every track passed: no clicks and no notes that vanish (check_song), a clean loop seam (check_loop, then "
		"fix_loop), the intro and the sides as above, the loudness by place.\n"
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

std::string StyleGuideTopic(const std::string &Name)
{
	for (size_t i = 0; i < SECTION_COUNT; ++i)
		if (Name == SECTIONS[i].Topic)
			return std::string(SECTIONS[i].Title) + "\n" + SECTIONS[i].Text + "\n";
	return std::string();
}

std::string StyleGuide(const std::string &Topic)
{
	const std::vector<std::string> Asked = SplitWords(Topic);
	if (Asked.empty()) {
		std::string Out = "The guide to the classic NES styles (Konami, Capcom, Nintendo, Square, Sunsoft, Technos), to dark "
			"and melancholic music, and to the atmospheric sound of modern FamiTracker music, from studies of their "
			"soundtracks, with recipes for calm songs, lively stages and action. Topics (references action guide with a topic):\n";
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

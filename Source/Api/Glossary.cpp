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

// The glossary (see Glossary.h)

#include "Glossary.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include "ChordRoles.h"
#include "Effects.h"
#include "Music.h"
#include "Song.h"
#include "SongShapes.h"

namespace ftapi {

namespace {

// An entry as written here: its other names and see-also ids comma separated
struct RawEntry {
	const char *Id;
	const char *Term;
	const char *Names;
	int Category;
	const char *Text;
	const char *SeeAlso;
	const char *Hear;
	const char *HearHow;
};

const RawEntry ENTRIES[] = {
	// Making music
	{ "note", "Note", "notes", GLOSSARY_MUSIC,
		"A note is one sound with a pitch and a length, like one key pressed on a piano. In FamiTracker a note is written "
		"like C-4: the letter is its name and the number its octave. A melody is notes one after another; a chord is several "
		"at once.",
		"pitch, octave, rest, melody, chord", "note", "" },
	{ "pitch", "Pitch (high and low)", "high and low, high, low, frequency", GLOSSARY_MUSIC,
		"Pitch is how high or low a note sounds: a bird's chirp is high, a tuba's note is low. Each piano key to the right is "
		"a little higher. In the piano roll higher notes sit higher up, and in the pattern editor a later letter or a bigger "
		"octave number is higher.",
		"note, octave, semitone", "pitch", "" },
	{ "octave", "Octave", "octaves", GLOSSARY_MUSIC,
		"Two notes an octave apart share a name and sound alike, the higher one vibrating exactly twice as fast. From one C "
		"to the next is an octave, twelve semitones. FamiTracker numbers the octaves after the note's name: C-3 is middle C "
		"and C-4 the C above it, and the Octave box picks the one your keyboard types.",
		"note, pitch, semitone, interval", "octave", "" },
	{ "semitone", "Semitone (half step)", "half step, halfstep, semitones, whole step, whole tone", GLOSSARY_MUSIC,
		"A semitone is the smallest step between two notes in most music: from one piano key to the very next, white or "
		"black. Two semitones make a whole step, and twelve make an octave. Effects like the arpeggio (0xy) and the note "
		"slides (Qxy, Rxy) count in semitones.",
		"interval, octave, sharp-flat, effect-0xy", "semitone", "" },
	{ "sharp-flat", "Sharp and flat", "sharp, flat, #, sharps, flats, accidental, accidentals, black keys", GLOSSARY_MUSIC,
		"A sharp (#) is a semitone higher than the plain letter, a flat (b) a semitone lower: C# and Db are the same black "
		"piano key. FamiTracker writes them with a #, like C#4. A key like G major uses one of them, F#, in place of F.",
		"semitone, scale, key", "sharp-flat", "" },
	{ "scale", "Scale", "scales, do re mi, major scale, minor scale", GLOSSARY_MUSIC,
		"A scale is the set of notes a piece uses, played in order from low to high like a staircase. The major scale (do re "
		"mi fa sol la ti do) sounds bright, the minor scale sadder. Notes picked from one scale sound like they belong "
		"together.",
		"key, major-minor, pentatonic, mode", "scale", "" },
	{ "pentatonic", "Pentatonic scale", "pentatonic, five-note scale", GLOSSARY_MUSIC,
		"A pentatonic scale has just five notes, leaving out the two that clash most easily, so almost anything played on it "
		"sounds fine together. Folk tunes, the blues and many game themes use it. The piano roll's Key menu has major and minor "
		"pentatonic keys.",
		"scale, key", "pentatonic", "" },
	{ "mode", "Mode", "modes, dorian, mixolydian, lydian, phrygian, aeolian, ionian", GLOSSARY_MUSIC,
		"Modes are scales that use a key's notes but make another note home, each with its own color: Dorian is minor with a "
		"brighter touch, Mixolydian major with a bluesy one, Lydian major with a dreamy one. Major and minor are modes too. The "
		"piano roll's Key menu offers them.",
		"scale, key, major-minor, home-note", "", "" },
	{ "key", "Key", "keys, tonality, home key, key of the song", GLOSSARY_MUSIC,
		"A song's key is its home note and the scale around it: in C major, C is home and the notes are the piano's white keys. "
		"Melodies and chords that stay in the key fit together, and a note outside it stands out like a surprise. The piano "
		"roll's Key button picks the key and shades the notes that fit.",
		"home-note, scale, major-minor, chord, chord-roles, piano-roll", "home", "" },
	{ "major-minor", "Major and minor", "major, minor, major key, minor key, major chord, minor chord, happy and sad",
		GLOSSARY_MUSIC,
		"Major and minor are the two moods most music uses. Major sounds bright and settled, like a sunny day; minor sounds "
		"darker, sad or mysterious. The difference is one note: in minor, the third note of the scale and of the home chord "
		"is a semitone lower.",
		"key, scale, third, chord", "major-minor", "" },
	{ "home-note", "Home note (tonic)", "tonic, home, keynote, do", GLOSSARY_MUSIC,
		"The home note is the note a song in a key feels settled on, like coming home after a walk: C in C major, A in A "
		"minor. Melodies sound finished when they end on it, and the chord built on it, the home chord, is where the music "
		"rests.",
		"key, chord-home, tension-release, chord-progression", "home", "" },
	{ "interval", "Interval", "intervals, distance between notes", GLOSSARY_MUSIC,
		"An interval is the distance between two notes, counted in semitones or in steps of the scale. Small ones sound smooth "
		"and close, big ones bold. Some have names: a third (three or four semitones), a fifth (seven) and an octave "
		"(twelve).",
		"semitone, step-leap, third, fifth, octave", "interval", "" },
	{ "step-leap", "Step and leap", "step, leap, steps, leaps, stepwise, jump in a melody", GLOSSARY_MUSIC,
		"A melody moves by steps, to the next note of the scale, or by leaps, jumping over notes. Steps sound smooth and "
		"singable, like walking; leaps sound bold, like a jump. Good melodies mix them, often a leap followed by steps back "
		"the other way.",
		"interval, melody, phrase", "step-leap", "" },
	{ "chord", "Chord", "chords, harmony notes", GLOSSARY_MUSIC,
		"A chord is three or more notes played together, like a hand on the piano keys. Chords give a song its mood under the "
		"melody, and the order they come in is the chord progression. On the NES a chord takes two or three channels, or one "
		"channel playing it as a fast arpeggio.",
		"triad, root, chord-progression, chord-roles, arpeggio, harmony", "chord", "" },
	{ "triad", "Triad", "triads, three-note chord", GLOSSARY_MUSIC,
		"A triad is the simplest chord: three notes, a root with the notes a third and a fifth above it. Major and minor "
		"triads are the chords most songs are made of; C major is C, E and G.",
		"chord, root, third, fifth, major-minor", "chord", "" },
	{ "root", "Root", "root note, chord root", GLOSSARY_MUSIC,
		"A chord's root is the note it's built on and named after: C is the root of C major and of C minor. A bass that plays "
		"the roots makes the chords sound solid, while another of the chord's notes at the bottom sounds lighter, as if moving "
		"on.",
		"chord, triad, bass-line", "root", "" },
	{ "third", "Third", "thirds, major third, minor third", GLOSSARY_MUSIC,
		"A third is the note two steps of the scale above another, three or four semitones up: the middle note of a triad. It "
		"decides a chord's mood: four semitones (a major third) sound bright, three (a minor third) darker. A harmony a third "
		"under the melody is the classic second voice.",
		"interval, triad, major-minor, harmony", "third", "" },
	{ "fifth", "Fifth", "fifths, perfect fifth, power chord", GLOSSARY_MUSIC,
		"A fifth is the note four steps of the scale above another, seven semitones up: the top of a triad. Root and fifth "
		"together sound strong and open, neither happy nor sad, which is why power chords and many bass lines use just the "
		"two.",
		"interval, triad, root, bass-line", "fifth", "" },
	{ "seventh", "Seventh chord", "seventh, sevenths, 7th, maj7, major seventh, dominant seventh, minor seventh, m7",
		GLOSSARY_MUSIC,
		"A seventh chord adds a fourth note to a triad, a seventh above the root. It makes the chord richer: a major seventh "
		"(Cmaj7) sounds dreamy and soft, a minor seventh (Am7) mellow, and a dominant seventh (G7) leans toward home, wanting to "
		"move on.",
		"chord, triad, tension-release", "seventh", "" },
	{ "chord-progression", "Chord progression", "progression, progressions, chord changes, changes, chord sequence, four chords",
		GLOSSARY_MUSIC,
		"A chord progression is a song's chords in order, often one or two a bar, repeating. Some are everywhere because they "
		"just work, like C, G, A minor and F. The progression carries the mood, like a short journey away from home and back.",
		"chord, chord-roles, home-note, tension-release, key", "progression", "" },
	{ "melody", "Melody (tune)", "tune, melodies, lead, lead line, main tune, top line", GLOSSARY_MUSIC,
		"The melody is the tune you'd hum or whistle: one note at a time, the part people remember. In chip music it usually "
		"plays on a pulse channel, on top of the chords and the bass and a little louder than them.",
		"hook, phrase, harmony, counter-melody", "melody", "" },
	{ "hook", "Hook (motif)", "motif, motive, riff, theme, idea, catchy bit", GLOSSARY_MUSIC,
		"A hook is a short, catchy bit of melody, a bar or two long, that the song keeps coming back to. A whole melody can "
		"grow from one: repeat it, move it higher or lower, change its ending.",
		"melody, phrase, repetition-variation", "hook", "" },
	{ "phrase", "Phrase", "phrases, musical sentence, sentence", GLOSSARY_MUSIC,
		"A phrase is a piece of melody that makes sense on its own, like a sentence: usually two or four bars, ending on a "
		"held note or a short rest where a singer would breathe. Melodies are phrases that answer each other, like a question "
		"and its answer.",
		"melody, rest, hook, bar", "phrase", "" },
	{ "rest", "Rest", "rests, silence, pause, gap, breath, breathing", GLOSSARY_MUSIC,
		"A rest is a moment of silence in a part. Rests give a melody room to breathe and make the notes around them stand "
		"out; a tune without any sounds breathless. In FamiTracker a note ends with a halt (---) or a release (===), and the "
		"rows after it are the rest.",
		"phrase, rhythm, release-halt", "rest", "" },
	{ "rhythm", "Rhythm", "rhythms, timing, note lengths", GLOSSARY_MUSIC,
		"Rhythm is how sounds fall in time: which notes are long and which short, and where they land against the beat. The "
		"same notes in another rhythm make another tune. Drums are almost all rhythm.",
		"beat, bar, syncopation, swing", "rhythm", "" },
	{ "beat", "Beat", "beats, the beat, count, quarter note, crotchet", GLOSSARY_MUSIC,
		"The beat is the steady pulse you tap your foot to. Music counts in beats, grouped into bars: one, two, three, four. "
		"In FamiTracker the first row highlight usually marks the beats, every four rows.",
		"bar, tempo, rhythm, highlight", "beat", "" },
	{ "bar", "Bar (measure)", "measure, bars, measures", GLOSSARY_MUSIC,
		"A bar is a group of beats, usually four, that repeats through a song like the lines of a page; its first beat is the "
		"strongest. Songs are counted in bars: a phrase is often two or four, a section eight. In FamiTracker the second row "
		"highlight marks the bars.",
		"beat, time-signature, phrase, highlight", "bar", "" },
	{ "time-signature", "Time signature (meter)", "meter, metre, 4/4, 3/4, 6/8, common time, waltz, triple time", GLOSSARY_MUSIC,
		"The time signature says how beats are grouped in bars. In 4/4, most music, there are four beats a bar; 3/4 has "
		"three, a waltz (ONE two three); 6/8 has two beats each split in three, for a rolling, galloping feel.",
		"bar, beat, rhythm", "meter", "" },
	{ "tempo", "Tempo (BPM)", "bpm, beats per minute, pace, how fast", GLOSSARY_MUSIC,
		"Tempo is how fast music goes, counted in beats per minute (BPM): 60 is a beat a second, a calm walk; 120 is brisk; "
		"160 and up is a chase. FamiTracker sets it with the song's Speed and Tempo, which the game music layout shows as "
		"one BPM box.",
		"beat, speed, tempo-setting", "tempo", "" },
	{ "swing", "Swing (shuffle)", "shuffle, swung, swing feel, triplet feel, straight", GLOSSARY_MUSIC,
		"Swing plays the notes between the beats a little late, so pairs of notes go long-short, long-short, like a skipping "
		"walk, instead of evenly. It sounds relaxed and bouncy; even, straight notes sound more driving. FamiTracker swings a "
		"song by alternating its speed every half beat, or one part by delaying notes with Gxx.",
		"rhythm, beat, groove, effect-gxx", "swing", "" },
	{ "syncopation", "Syncopation", "syncopated, offbeat, off-beat, push, anticipation", GLOSSARY_MUSIC,
		"Syncopation puts notes where you don't expect them: between the beats, or held across a strong one. It makes music "
		"feel lively and funky, like a skip in a step. With too much of it the beat gets lost.",
		"rhythm, beat, accent", "syncopation", "" },
	{ "accent", "Accent", "accents, stress, emphasis, accented", GLOSSARY_MUSIC,
		"An accent is a note played louder than the ones around it. Accents on the beats make a rhythm clear, and a few on "
		"surprising notes make it exciting. In FamiTracker an accent is a higher number in the volume column.",
		"dynamics, rhythm, volume-column", "accent", "" },
	{ "dynamics", "Dynamics (loud and soft)", "loud, quiet, loudness, crescendo, decrescendo, swell, fade", GLOSSARY_MUSIC,
		"Dynamics are the changes between loud and soft. A song that's loud all the time gets tiring, so a quieter part makes "
		"the next one hit harder, and a note can swell in or fade out. In FamiTracker the volume column, the instrument's "
		"volume and the Axy effect make them.",
		"accent, volume-column, effect-axy, mix", "dynamics", "" },
	{ "staccato-legato", "Staccato and legato", "staccato, legato, short notes, smooth notes, detached, connected",
		GLOSSARY_MUSIC,
		"Staccato notes are short and detached, with little gaps between them, like a bouncing ball; legato notes are smooth "
		"and joined, each running into the next, like a singer holding a line. The same tune sounds playful one way and calm "
		"the other.",
		"rest, rhythm, effect-sxx, effect-3xx", "staccato-legato", "" },
	{ "bass-line", "Bass line", "bass, bassline, basses, low part, bass part", GLOSSARY_MUSIC,
		"The bass line is the lowest part, one note at a time, mostly playing the chords' roots and the notes between them. It "
		"holds the harmony up and drives the rhythm with the drums. On the NES the triangle is the classic bass.",
		"root, triangle, groove, chord", "bass", "" },
	{ "harmony", "Harmony", "harmonies, harmony voice, second voice, harmonize, harmonise", GLOSSARY_MUSIC,
		"Harmony is the notes that sound together with the melody: its chords, or a second line that follows the tune a third "
		"or a sixth below. Good harmony makes a melody sound fuller without covering it.",
		"chord, third, counter-melody, melody", "harmony", "" },
	{ "counter-melody", "Counter-melody", "countermelody, counter melody, counter-melodies, second melody, answer",
		GLOSSARY_MUSIC,
		"A counter-melody is a second tune played against the main one: it moves when the melody holds a note and rests when "
		"the melody is busy, like two people taking turns to talk. It adds interest without fighting the lead.",
		"melody, harmony, layers", "counter-melody", "" },
	{ "arpeggio", "Arpeggio (broken chord)", "arpeggios, broken chord, arp, arpeggiated", GLOSSARY_MUSIC,
		"An arpeggio plays a chord's notes one after another instead of together, like a finger running up a harp. Slowly it "
		"makes a flowing accompaniment; very fast on one channel it blurs into a whole chord, the classic chip trick of the "
		"0xy effect.",
		"chord, effect-0xy, arpeggio-sequence", "arpeggio", "" },
	{ "drum-kit", "Drum kit", "drums, drum, kit, percussion", GLOSSARY_MUSIC,
		"A drum kit is the set of drums a drummer plays: the kick (a low thump), the snare (a sharp crack), the hi-hats "
		"(ticking), cymbals (crashes) and toms (drums with a pitch). On the NES they come from the noise channel, short "
		"triangle notes and DPCM samples.",
		"kick, snare, hi-hat, crash, tom, groove, noise", "drums", "" },
	{ "kick", "Kick drum", "kick, bass drum, kick drum, bassdrum", GLOSSARY_MUSIC,
		"The kick is the drum kit's low thump, the one you feel: usually on the first beat of a bar and often the third. On "
		"the NES it's a quick low burst of noise, a short triangle note that drops in pitch, or a sample.",
		"drum-kit, snare, groove", "kick", "" },
	{ "snare", "Snare drum", "snare, snare drum, backbeat", GLOSSARY_MUSIC,
		"The snare is the sharp crack of a drum kit, usually on beats two and four (the backbeat), answering the kick. On the "
		"NES it's a burst of noise in its middle pitches that dies away fast, sometimes with a sample.",
		"drum-kit, kick, beat", "snare", "" },
	{ "hi-hat", "Hi-hat", "hihat, hi hat, hat, hats, open hi-hat, closed hi-hat", GLOSSARY_MUSIC,
		"The hi-hat is the small ticking cymbal that keeps time between the kick and the snare, often on every eighth note. "
		"Closed it's a short tick, open a longer hiss. On the NES it's a very short burst of high noise.",
		"drum-kit, crash, noise", "hi-hat", "" },
	{ "crash", "Crash cymbal", "crash, cymbal, cymbals, ride", GLOSSARY_MUSIC,
		"A crash is a big cymbal hit that rings and fades, marking the start of a new part or a big moment. On the NES it's a "
		"long hiss of high noise slowly dying away.",
		"drum-kit, hi-hat, fill", "crash", "" },
	{ "tom", "Tom", "toms, tom-tom, tom drum", GLOSSARY_MUSIC,
		"Toms are drums with a pitch, from high to low, often played in a quick run downward for a fill. On the NES they're "
		"short triangle or noise notes that drop in pitch.",
		"drum-kit, fill, triangle", "tom", "" },
	{ "fill", "Fill (drum fill)", "fills, drum fill, drum roll, roll", GLOSSARY_MUSIC,
		"A fill is a short burst of drums, a beat or two at the end of a phrase, that leads into the next part, like a drum "
		"roll before an announcement. It tells the listener something new is coming.",
		"drum-kit, tom, groove, song-form", "fill", "" },
	{ "groove", "Groove", "grooves, feel, backing beat, drum pattern", GLOSSARY_MUSIC,
		"A groove is the repeating pattern of drums and bass that makes you want to move, the feel of the rhythm more than its "
		"notes. Edit > Start from a groove writes one for a mood, with a bass and chords to build on.",
		"drum-kit, bass-line, swing, rhythm", "groove", "" },
	// (A song's form and its parts, in Module > Song shape's words, are built with the chords' roles in Build)
	{ "loop", "Loop", "looping, loop point, seamless loop", GLOSSARY_MUSIC,
		"Game music loops: when the song reaches its end it goes back to a point near its start and plays on, for as long as "
		"the level lasts. A good loop joins without a bump, so nobody hears where it starts again. In FamiTracker a jump "
		"effect (Bxx) on the last frame sets where it loops to.",
		"song-form, intro, effect-bxx", "", "" },
	{ "repetition-variation", "Repetition and variation", "repetition, variation, variations, repeat, repeats, "
		"theme and variations", GLOSSARY_MUSIC,
		"Music repeats things so the ear can follow them, and changes them so they stay interesting: play a phrase, then play "
		"it again with a new ending. Too much repetition gets boring, too little sounds like it's going nowhere.",
		"hook, phrase, song-form", "repetition", "" },
	{ "tension-release", "Tension and release", "tension, resolution, resolve, suspense", GLOSSARY_MUSIC,
		"Music builds tension, a feeling that something has to happen next, and then releases it by landing somewhere at "
		"rest. A chord that leans toward home, like G7 in C major, is tension, and arriving home is the release. Songs move "
		"between the two like breathing in and out.",
		"home-note, chord-tension, seventh, chord-progression, clash", "tension", "" },
	{ "clash", "Clash (dissonance)", "clashes, dissonance, dissonant, rub, sour note, wrong note", GLOSSARY_MUSIC,
		"A clash is two notes that rub against each other when they sound together, like two a semitone apart: sour or "
		"tense. A short one passing by adds spice; a long one usually sounds like a mistake. View > Problems lists the "
		"clashes in a song.",
		"semitone, tension-release, harmony", "clash", "" },
	{ "layers", "Layers (texture)", "layer, layering, texture", GLOSSARY_MUSIC,
		"Layers are the parts playing at once: melody, chords, bass, drums, an echo. Adding layers makes music fuller, and "
		"taking some away for a while makes their return hit harder. Each NES channel plays one layer.",
		"mix, song-form, break, channel", "layers", "" },
	{ "echo", "Echo", "delay, echo channel, echoes", GLOSSARY_MUSIC,
		"An echo is the melody played again a moment later and softer, like a voice in a canyon. NES composers make it by "
		"copying the lead to a free channel an eighth note or so later, at about half the volume; the mixer's Space can add "
		"one too.",
		"reverb, space, layers", "echo", "" },
	{ "reverb", "Reverb", "reverberation, room, hall, ambience, ambiance", GLOSSARY_MUSIC,
		"Reverb is the sound of a room: notes ring on and fade into the walls, like singing in a church or a bathroom. The NES "
		"has none, but this FamiTracker's mixer adds one: a channel's Space knob sends it into a room chosen at the top of the "
		"mixer.",
		"space, echo, mixer", "", "Turn a channel's Space knob up in View > Mixer, then play the song." },
	{ "panning", "Panning (stereo)", "pan, stereo, left and right, wide, mono", GLOSSARY_MUSIC,
		"Panning places a part between the left and right speakers, so not everything comes from the middle. The NES is mono, "
		"but this FamiTracker can pan each channel (the 8xx effect, or the pan in View > Mixer). An echo on one side and chords "
		"spread apart sound wide, while the bass and the lead stay in the middle.",
		"effect-8xx, mixer, mix", "panning", "" },
	{ "mix", "Mix (balance)", "mixing, balance, levels, loudness balance", GLOSSARY_MUSIC,
		"The mix is how loud each part is against the others. In a good mix the melody is clear on top, the bass is solid, "
		"the chords sit under the tune and the drums cover nothing. Turn the other parts down rather than everything up.",
		"dynamics, layers, mixer, volume-column", "mix", "" },

	// Sounds
	{ "timbre", "Timbre (tone color)", "tone color, tone colour, sound color, tone, color, colour, sound", GLOSSARY_SOUNDS,
		"Timbre is what makes two sounds differ on the same note, like a flute and a trumpet playing one pitch. On the NES it "
		"comes from the wave each channel plays (pulse, triangle, noise) and from how the instrument shapes it over the "
		"note.",
		"pulse-width, envelope, waveform, instrument", "timbre", "" },
	{ "envelope", "Envelope (attack, decay, sustain, release)", "envelopes, attack, decay, sustain, adsr, volume envelope, "
		"volume shape", GLOSSARY_SOUNDS,
		"An envelope is how a sound's loudness changes over a note: the attack (how fast it starts), the decay (how it falls "
		"after that), the sustain (the level it holds) and the release (how it fades once the note is let go). A pluck starts "
		"at once and dies quickly, a pad swells in slowly. An instrument's volume sequence draws it.",
		"volume-sequence, pluck, pad, release-halt", "envelope", "" },
	{ "vibrato", "Vibrato", "vibratos, wobble, pitch wobble, wavering", GLOSSARY_SOUNDS,
		"Vibrato is a gentle wobble in a note's pitch, like a singer's voice holding a long note. A little, brought in after "
		"the note starts, makes a lead sing; a lot sounds wobbly or comic. The 4xy effect and an instrument's pitch sequence "
		"make it.",
		"effect-4xy, pitch-sequence, slide", "vibrato", "" },
	{ "slide", "Slide (glide, bend)", "pitch slide, glide, bend, pitch bend, scoop, fall, glissando", GLOSSARY_SOUNDS,
		"A slide moves smoothly from one pitch to another instead of jumping: up into a note (a scoop), down at the end of a "
		"phrase (a fall), or gliding between every note (portamento). The effects 1xx, 2xx, 3xx, Qxy and Rxy make them.",
		"effect-qxy, effect-rxy, effect-3xx, vibrato", "slide", "" },
	{ "detune", "Detune (chorus effect)", "detuned, chorus effect, beating, unison", GLOSSARY_SOUNDS,
		"Detuning plays two copies of a part a tiny bit apart in pitch, so they shimmer and beat against each other and "
		"sound thicker than one. In FamiTracker, put the same notes on two channels with Pxx on one of them (P81 or P82).",
		"effect-pxx, layers, echo, chorus", "detune", "" },
	{ "pulse-width", "Pulse width (duty)", "duty, duty cycle, width, widths, 12.5%, 25%, 50%, 75%, square wave", GLOSSARY_SOUNDS,
		"A pulse channel can change the shape of its wave, its width, and with it its tone: 12.5% is thin and nasal, 25% "
		"bright and full, 50% (a square) round and hollow, like a clarinet. The Vxx effect or an instrument's duty sequence "
		"picks it, and the VRC6's pulses have eight widths.",
		"pulse, effect-vxx, duty-sequence, timbre", "pulse-width", "" },
	{ "pulse", "Pulse channel", "pulse, square, pulse 1, pulse 2, square channel, pulse wave, squares", GLOSSARY_SOUNDS,
		"The 2A03 has two pulse channels, which play a bright, buzzy wave at 16 volumes and four widths. They carry most "
		"melodies and chords in NES music; the VRC6 and the MMC5 bring more of them.",
		"pulse-width, triangle, noise, 2a03", "pulse", "" },
	{ "triangle", "Triangle channel", "triangle, triangle wave, tri", GLOSSARY_SOUNDS,
		"The triangle plays a soft, round wave with no volume control, only on or off: deep and smooth, it's the classic NES "
		"bass, and short falling notes on it make kicks and toms. It sounds an octave lower than a pulse playing the same "
		"note.",
		"bass-line, pulse, noise, 2a03", "triangle", "" },
	{ "noise", "Noise channel", "noise, white noise, hiss", GLOSSARY_SOUNDS,
		"The noise channel plays hiss instead of notes, from low rumbles to high hisses: the NES's drums (hi-hats, snares, "
		"crashes) and sounds like wind, rain and explosions. Its second mode (V01) gives a metallic, buzzing tone.",
		"drum-kit, hi-hat, snare, 2a03, effect-vxx", "noise", "" },
	{ "sawtooth", "Sawtooth (VRC6)", "saw, saw wave, sawtooth wave", GLOSSARY_SOUNDS,
		"The VRC6's sawtooth plays a bright, buzzy wave full of overtones, like brass or a synth: great for a strong bass line "
		"or a bold lead. It needs the VRC6 chip.",
		"vrc6, pulse, bass-line", "sawtooth", "" },
	{ "dpcm", "DPCM samples", "dpcm, samples, sample, sampled drums, dmc, delta modulation", GLOSSARY_SOUNDS,
		"The DPCM channel plays short recorded sounds, samples, in the NES's own rough format: drums with real punch, a "
		"voice, a bass hit. Samples take a lot of a cartridge's memory, so they're short. Its level (the Zxx effect) also "
		"makes the triangle and the noise quieter.",
		"drum-kit, effect-zxx, 2a03", "",
		"Open Instrument > Sound browser and pick a drum kit with samples (it plays them there), then play notes on the DPCM "
		"channel." },
	{ "waveform", "Wave (waveform)", "wave, waves, wave shape, wavetable, wavetables", GLOSSARY_SOUNDS,
		"A wave is the shape a channel repeats many times a second to make its sound, and the shape decides the tone. The "
		"2A03's waves are fixed (pulse, triangle), but the FDS and the N163 play waves you draw, so they can sound like organs, "
		"flutes or choirs.",
		"timbre, fds, n163, pulse-width", "timbre", "" },
	{ "pad", "Pad", "pads, string pad, choir pad, warm pad", GLOSSARY_SOUNDS,
		"A pad is a soft, held sound that fills the background with chords, like strings or a choir. Slow to start and slow to "
		"fade, it sits quietly under the melody.",
		"envelope, chord, pluck, layers", "envelope", "" },
	{ "pluck", "Pluck", "plucks, plucked, pluck sound", GLOSSARY_SOUNDS,
		"A pluck is a sound that starts sharp and dies away quickly, like a plucked string or a music box. It suits arpeggios, "
		"bouncy chords and gentle melodies.",
		"envelope, pad, staccato-legato", "envelope", "" },
	{ "2a03", "2A03 (the NES's own chip)", "rp2a03, apu, nes chip, nes sound chip, famicom chip", GLOSSARY_SOUNDS,
		"The 2A03 is the sound chip inside every NES and Famicom: two pulse channels, a triangle, the noise and the DPCM. "
		"Every song has it, and the expansion chips add channels to it.",
		"pulse, triangle, noise, dpcm, expansion-chip", "2a03", "" },
	{ "vrc6", "VRC6", "konami vrc6", GLOSSARY_SOUNDS,
		"Konami's VRC6, from the Japanese Castlevania III, adds two more pulses with eight widths and a sawtooth: a fuller, "
		"brighter sound, a favorite for rich chip music. The game music profile starts new songs with it.",
		"sawtooth, pulse-width, vrc6-plus, expansion-chip", "vrc6", "" },
	{ "vrc6-plus", "VRC6 Plus", "soft layer, soft 1, soft channels", GLOSSARY_SOUNDS,
		"VRC6 Plus is this FamiTracker's own chip: the VRC6 with a soft layer under it, four wave channels (Soft 1 to Soft 4) "
		"for pads, a soft echo or a deep bass. It's made for the game's sound files: no real cartridge has both chips.",
		"vrc6, n163, pad", "vrc6-plus", "" },
	{ "vrc7", "VRC7", "fm, fm synthesis, opll", GLOSSARY_SOUNDS,
		"Konami's VRC7 adds six FM channels with a smoother sound than the other chips, like an electric piano, an organ or a "
		"synth: 15 built-in sounds and one you design. Only one game used it, Lagrange Point.",
		"expansion-chip, instrument", "vrc7", "" },
	{ "fds", "FDS (Famicom Disk System)", "disk system, famicom disk system", GLOSSARY_SOUNDS,
		"The Famicom Disk System's chip adds one channel that plays a wave you draw, with a modulator that can bend its pitch. "
		"Its tones are soft, haunting or bell-like, heard in many Famicom disk games.",
		"waveform, expansion-chip, effect-hxx-fds", "fds", "" },
	{ "n163", "N163 (Namco 163)", "namco, namco 163, n106, wave channels", GLOSSARY_SOUNDS,
		"Namco's N163 adds up to eight channels that play small waves you draw: organs, choirs, soft pads and glassy tones. "
		"The more of its channels a song uses, the quieter and grainier each one gets, so four is common.",
		"waveform, vrc6-plus, expansion-chip, wave-sequence", "n163", "" },
	{ "mmc5", "MMC5", "nintendo mmc5", GLOSSARY_SOUNDS,
		"Nintendo's MMC5 adds two pulse channels just like the 2A03's, without the sweep. They give more room for chords and "
		"harmonies with the familiar sound.",
		"pulse, expansion-chip", "mmc5", "" },
	{ "5b", "5B (Sunsoft 5B)", "sunsoft 5b, sunsoft, ym2149, ay", GLOSSARY_SOUNDS,
		"Sunsoft's 5B adds three square channels that can also play noise and a buzzing envelope, a sound all its own. NSF "
		"files can't carry it here, but WAV files and the game's OGG files can.",
		"expansion-chip, effect-hxy-5b, mode-sequence-5b", "5b", "" },
	{ "expansion-chip", "Expansion chip", "expansion, expansion chips, extra chip, chips, chip", GLOSSARY_SOUNDS,
		"An expansion chip is a sound chip some game cartridges added to the NES's own, for more channels and new sounds: the "
		"VRC6, VRC7, FDS, N163, MMC5 and 5B. Module > Module properties turns them on.",
		"2a03, vrc6, vrc7, fds, n163, mmc5, 5b", "", "" },

	// The tracker
	{ "channel", "Channel", "channels, voice, column", GLOSSARY_TRACKER,
		"A channel is one voice of a sound chip: it plays one note at a time. The 2A03 has five, and each column of the "
		"pattern editor is a channel, so a chord needs several channels or an arpeggio on one.",
		"pattern, 2a03, expansion-chip, layers", "", "" },
	{ "row", "Row", "rows, line", GLOSSARY_TRACKER,
		"A row is one line of the pattern editor, a small step of time in which each channel can start a note. At speed 6 and "
		"tempo 150 a row lasts a tenth of a second, and four rows often make a beat.",
		"pattern, speed, highlight, beat", "", "" },
	{ "pattern", "Pattern", "patterns, pattern editor", GLOSSARY_TRACKER,
		"A pattern is a block of rows for one channel, like a page of sheet music: its notes, instruments, volumes and "
		"effects. Frames put patterns in order, and one pattern can play in several frames, so a part that repeats is "
		"written once.",
		"frame, row, channel, pattern-length", "", "" },
	{ "frame", "Frame", "frames, order, order list, frame list, frame editor, pattern order", GLOSSARY_TRACKER,
		"A frame is one step of the song's order: a row of pattern numbers, one for each channel, in the frame list at the top "
		"left. The song plays its frames from top to bottom, and a frame is often a few bars long. The Frames box sets how "
		"many there are.",
		"pattern, pattern-length, sections, loop", "", "" },
	{ "speed", "Speed", "speed setting, ticks per row, song speed", GLOSSARY_TRACKER,
		"Speed is how many ticks each row lasts, so a bigger speed is slower: speed 6 at tempo 150 is a row every tenth of a "
		"second. The Fxx effect changes it inside a song. The game music layout shows speed and tempo together as BPM.",
		"tempo-setting, tempo, tick, row, effect-fxx", "", "" },
	{ "tempo-setting", "Tempo (the setting)", "tempo setting, tempo box", GLOSSARY_TRACKER,
		"The Tempo box stretches or squeezes the speed's ticks: at 150 rows last as long as the speed says, at 75 twice as "
		"long, at 300 half as long. Most songs leave it at 150 and set the pace with Speed, or let the BPM box choose both.",
		"speed, tempo, effect-fxx", "", "" },
	{ "tick", "Tick", "ticks, engine frame", GLOSSARY_TRACKER,
		"A tick is FamiTracker's smallest step of time, a sixtieth of a second (a fiftieth on a PAL machine), as often as the "
		"NES draws its screen. Effects and instruments change a little every tick, and a row lasts as many ticks as the "
		"speed.",
		"speed, row, sequence", "", "" },
	{ "pattern-length", "Rows (pattern length)", "pattern length, rows setting, rows box", GLOSSARY_TRACKER,
		"The Rows box sets how many rows each pattern has, so how long every frame lasts: 64 rows at four a beat is 16 beats, "
		"four bars of 4/4. Longer patterns hold more of the song in one frame; shorter ones repeat more easily.",
		"row, pattern, frame", "", "" },
	{ "highlight", "Row highlight", "highlights, highlighted rows, beat highlight, bar highlight", GLOSSARY_TRACKER,
		"The pattern editor colors rows to show the beats and the bars: the first highlight every few rows (often 4, a beat), "
		"the second every few more (often 16, a bar). They're a guide for your eyes, and the BPM counts the first as a beat.",
		"beat, bar, row", "", "" },
	{ "instrument", "Instrument", "instruments, patch, preset, presets, fti", GLOSSARY_TRACKER,
		"An instrument is a sound you play notes with: which chip it's for, and the sequences that shape its volume, pitch and "
		"tone over each note. Every note names its instrument, the two digits after the note. Instrument > Sound browser "
		"offers ready-made ones.",
		"sequence, envelope, timbre, sound-browser", "", "" },
	{ "sequence", "Sequence (macro)", "sequences, macro, macros, instrument sequence", GLOSSARY_TRACKER,
		"A sequence is a list of numbers an instrument steps through, a tick at a time, while a note plays: its volume, "
		"arpeggio, pitch, hi-pitch and duty sequences shape the note. A loop point repeats part of it while the note is held, "
		"and a release point plays the rest once the note is released.",
		"instrument, volume-sequence, arpeggio-sequence, pitch-sequence, duty-sequence, tick", "", "" },
	{ "volume-sequence", "Volume sequence", "volume macro", GLOSSARY_TRACKER,
		"The volume sequence draws how loud each tick of a note is, from 0 to 15: high then falling is a pluck, rising slowly "
		"a swell, steady an organ. The note's volume column scales it.",
		"sequence, envelope, volume-column", "envelope", "" },
	{ "arpeggio-sequence", "Arpeggio sequence", "arpeggio macro, arp sequence, arp macro", GLOSSARY_TRACKER,
		"The arpeggio sequence moves the note by semitones each tick: 0 4 7 looping plays a quick major chord on one channel, "
		"and a fast drop from 12 to 0 makes a zap. Its setting picks whether the numbers are added to the note, are notes of "
		"their own or add up step by step.",
		"sequence, arpeggio, effect-0xy", "arpeggio", "" },
	{ "pitch-sequence", "Pitch sequence", "pitch macro", GLOSSARY_TRACKER,
		"The pitch sequence nudges the pitch a little each tick, and the nudges add up: a loop of small ups and downs is a "
		"vibrato, a steady one a slide, a big fall a drum's drop. Its steps are tiny, so small numbers already do a lot.",
		"sequence, vibrato, slide, hi-pitch-sequence", "vibrato", "" },
	{ "hi-pitch-sequence", "Hi-pitch sequence", "hi-pitch, hipitch, hi pitch, hi-pitch macro", GLOSSARY_TRACKER,
		"The hi-pitch sequence works like the pitch sequence, but in steps sixteen times bigger. It's for big sweeps, sirens "
		"and sound effects rather than gentle wobbles.",
		"pitch-sequence, sequence, slide", "", "" },
	{ "duty-sequence", "Duty / noise sequence", "duty sequence, duty macro, noise sequence, duty / noise, duty/noise",
		GLOSSARY_TRACKER,
		"The duty sequence changes a pulse's width each tick, or the noise's mode, so a sound can start thin and turn round, or "
		"flip between widths for a buzzing, talking tone. While it runs it overrides the Vxx effect.",
		"pulse-width, sequence, effect-vxx", "pulse-width", "" },
	{ "wave-sequence", "Wave sequence (N163)", "wave sequence, wave macro, n163 wave", GLOSSARY_TRACKER,
		"On the N163, an instrument's wave sequence picks which of its drawn waves plays each tick, so a sound can change its "
		"tone as it goes, brightening or darkening. With one wave it isn't needed.",
		"n163, waveform, sequence", "", "" },
	{ "mode-sequence-5b", "Noise / mode sequence (5B)", "noise / mode, mode sequence, 5b mode, noise/mode", GLOSSARY_TRACKER,
		"On the 5B, this sequence picks what each tick plays: the square, the noise, the envelope or a mix, and the noise's "
		"pitch. With it one 5B channel can be a drum one moment and a tone the next.",
		"5b, sequence, effect-hxy-5b", "", "" },
	{ "volume-column", "Volume column", "volume, vol, volumes, note volume", GLOSSARY_TRACKER,
		"The volume column, the digit after a note's instrument, sets how loud the channel plays from there on, from F (15, "
		"full) down to 1, and 0 for silence. It scales the instrument's own volume and lasts until another one is written. The "
		"triangle ignores it, except 0.",
		"dynamics, accent, volume-sequence, mix", "", "" },
	{ "effect-column", "Effect columns", "effect, effects, effect column, fx", GLOSSARY_TRACKER,
		"After the volume, each channel has up to four effect columns: a letter or digit and two more digits, like 447, that "
		"change how the notes play (vibrato, slides, arpeggios, cuts). Most last until turned off with 00. Hover over an "
		"effect in the pattern editor to read what it does.",
		"effect-4xy, effect-0xy, effect-qxy, effect-sxx", "", "" },
	{ "release-halt", "Release and halt (note off)", "release, halt, note off, note-off, ===, ---, stop note, key off",
		GLOSSARY_TRACKER,
		"A halt (---) stops a channel's note at once. A release (===) lets the note fade the way its instrument's release part "
		"says, like letting go of a piano key. Either one ends a note, and the empty rows after it are a rest.",
		"rest, envelope, sequence, effect-sxx", "", "" },
	{ "piano-roll", "Piano roll", "pianoroll, roll view, note view", GLOSSARY_TRACKER,
		"The piano roll shows the notes as bars on a grid, higher notes higher up and longer notes longer, like the roll of a "
		"player piano. You can draw, move and stretch notes there instead of typing them. View > Piano roll shows it.",
		"pattern, key, note", "", "" },
	{ "sections", "Sections (named parts)", "section, named sections, song sections, section names", GLOSSARY_TRACKER,
		"Sections name a song's parts (Intro, A, B, Break) over its frames, so you and the tools can talk about them. "
		"Right-click the frame list to name one, or let Module > Song shape lay out a whole song's. The frame list and the "
		"piano roll's ruler show them, and the mouse resting on one says what the part is for.",
		"song-form, frame, a-part, piano-roll", "", "" },
	{ "clips", "Clips", "clip, clips panel, ideas, saved ideas", GLOSSARY_TRACKER,
		"A clip is a saved piece of music, a recorded idea, a riff or a bar of drums, kept beside the song in the clips panel. "
		"Drag one into the pattern editor to use it, as a copy or as a linked copy that changes with the clip.",
		"pattern, piano-roll", "", "" },
	{ "track", "Track (song)", "song, songs, tracks, subtune, subsong", GLOSSARY_TRACKER,
		"A module can hold several tracks, songs that share its instruments, each with its own frames, speed and tempo: a "
		"stage's music, a boss theme and a jingle in one file. The Songs box picks the one you're working on.",
		"module, frame", "", "" },
	{ "module", "Module (.ftm file)", "ftm, ftm file, file", GLOSSARY_TRACKER,
		"A module is a FamiTracker file (.ftm): its tracks, instruments and samples together. File > Export for games turns "
		"its tracks into sound files for a game.",
		"track, instrument", "", "" },
	{ "edit-step", "Edit step", "step size, key step, step box", GLOSSARY_TRACKER,
		"The Step box sets how many rows the cursor jumps after you type a note: 1 moves to the next row, 4 to the next beat "
		"when four rows make one. 0 keeps it where it is.",
		"row, highlight", "", "" },
	{ "octave-box", "Octave (the box)", "octave box, edit octave, keyboard octave", GLOSSARY_TRACKER,
		"The Octave box picks the octave your keyboard's note keys type in: the Z row types notes from it and the Q row from "
		"the octave above. At 3 the Z key types C-3, middle C. Change it to reach higher or lower notes.",
		"octave, note, edit-step", "", "" },
	{ "mixer", "Mixer", "fader, faders, solo, mute", GLOSSARY_TRACKER,
		"View > Mixer gives every channel a strip: a fader for its level, its pan, mute and solo, and the Soft and Space knobs. "
		"Its settings are saved with the song and heard in FamiTracker, WAV files and the game's sound files, not in NSF "
		"files.",
		"mix, soft, space, panning", "", "" },
	{ "soft", "Soft (the mixer's knob)", "soft knob, soft, low-pass, lowpass, muffle", GLOSSARY_TRACKER,
		"A channel's Soft knob in the mixer rounds off its sound, taking the edge off its brightest buzz like a cloth over a "
		"speaker: 0 leaves the chip's sound as it is, and the higher it goes, the warmer and more muffled. It doesn't change "
		"the notes, only how they sound.",
		"mixer, space, timbre", "", "Turn a channel's Soft knob up in View > Mixer, then play the song." },
	{ "space", "Space (the mixer's knob)", "space knob, space, send, reverb send", GLOSSARY_TRACKER,
		"A channel's Space knob in the mixer sends it into the song's space: a room's reverb and an echo, chosen at the top of "
		"the mixer. The higher the knob, the more of the channel rings on there; 0 sends nothing.",
		"reverb, echo, mixer, soft", "", "Turn a channel's Space knob up in View > Mixer, then play the song." },
	{ "sound-browser", "Sound browser", "sounds, library, instrument library", GLOSSARY_TRACKER,
		"Instrument > Sound browser lists every sound the song can get: ready-made ones for each chip, sounds studied from "
		"games and the song's own. Click one to hear it, and add it to the song as an instrument.",
		"instrument, expansion-chip", "", "" },
};

}	// namespace

namespace {

// The notes as FamiTracker writes them ("E-3", "C#4", "Bb2"): its pitch, C-0 = 0
int P(const char *Name)
{
	int Pitch = 0;
	ParsePitch(Name, Pitch);
	return Pitch;
}

// An example's notes, written in ms from its start; Finish leaves every voice halted, puts the pulses at a bright 25%
// width unless the example sets one, and starts it all 40 ms in (the first notes then reach the channels together)
class Score {
public:
	Score(HearExample &Out, const char *Id, const char *Says, const char *Chip = "") : m_Out(Out)
	{
		m_Out = HearExample();
		m_Out.Id = Id;
		m_Out.Says = Says;
		m_Out.Chip = Chip;
	}
	void Note(int At, int Voice, int Pitch, int Volume, int Effect = EFF_NONE, int Param = 0)
	{
		const HearEvent E = { At, Voice, Pitch, Volume, Effect, Param };
		m_Out.Events.push_back(E);
		m_Owner.push_back(Pitch >= 0 ? ++m_iNotes : -1);
		m_Tail.push_back(false);
	}
	void Halt(int At, int Voice)
	{
		Note(At, Voice, HEAR_HALT, -1);
	}
	void Set(int At, int Voice, int Volume, int Effect = EFF_NONE, int Param = 0)
	{
		Note(At, Voice, HEAR_NONE, Volume, Effect, Param);
	}
	// Notes one after another from At, each Each ms apart, held for Hold ms (0: until the next), halted after the last
	int Line(int At, int Voice, const std::vector<int> &Pitches, int Each, int Volume, int Hold = 0)
	{
		for (size_t i = 0; i < Pitches.size(); ++i) {
			Note(At, Voice, Pitches[i], Volume);
			if (Hold > 0 && Hold < Each)
				Halt(At + Hold, Voice);
			At += Each;
		}
		if (Hold == 0 || Hold >= Each)
			Halt(At, Voice);
		return At;
	}
	// A drum hit: a noise pitch (or a note) at Volume, falling away in steps of Step ms to silence
	// (A hit's fading tail ends where the voice's next note starts)
	void Hit(int At, int Voice, int Pitch, int Volume, int Steps, int Step = 40)
	{
		Note(At, Voice, Pitch, Volume);
		const int Owner = m_iNotes;
		for (int i = 1; i <= Steps; ++i) {
			const int Left = Volume * (Steps - i) / Steps;
			if (Left > 0)
				Set(At + i * Step, Voice, Left);
			else
				Halt(At + i * Step, Voice);
			m_Owner.back() = Owner;
			m_Tail.back() = true;
			if (Left <= 0)
				break;
		}
	}
	// The NES's kick: a low thump of noise and a short triangle note falling fast (either alone where the other channel
	// plays something else)
	void Kick(int At, int Volume = 15, bool Noise = true, bool Triangle = true)
	{
		if (Noise)
			Hit(At, HEAR_NOISE, 0x3, Volume, 3, 40);
		if (Triangle) {
			Note(At, HEAR_TRIANGLE, P("G-3"), 15, EFF_PORTA_DOWN, 0x60);
			Halt(At + 80, HEAR_TRIANGLE);
		}
	}
	void Snare(int At, int Volume = 13)
	{
		Hit(At, HEAR_NOISE, 0xA, Volume, 5, 40);
	}
	void Hat(int At, int Volume = 8)
	{
		Hit(At, HEAR_NOISE, 0xD, Volume, 1, 40);
	}
	void Finish()
	{
		std::vector<HearEvent> &E = m_Out.Events;
		{
			// In time order; a hit's tail stops where a newer note on its voice starts, and a halt right before a note of
			// its voice goes (the note stops what played anyway)
			std::vector<size_t> Order(E.size());
			for (size_t i = 0; i < Order.size(); ++i)
				Order[i] = i;
			std::stable_sort(Order.begin(), Order.end(), [&E](size_t a, size_t b) { return E[a].At < E[b].At; });
			std::map<int, int> Playing;		// a voice's newest note (its owner number)
			std::vector<HearEvent> Kept;
			for (size_t k = 0; k < Order.size(); ++k) {
				const size_t i = Order[k];
				if (E[i].Note >= 0)
					Playing[E[i].Voice] = m_Owner[i];
				else if (m_Tail[i] && Playing[E[i].Voice] != m_Owner[i])
					continue;
				if ((E[i].Note == HEAR_HALT || m_Tail[i]) && E[i].Effect == EFF_NONE) {
					bool NoteNext = false;
					for (size_t j = k + 1; j < Order.size() && E[Order[j]].At - E[i].At < 25; ++j)
						NoteNext = NoteNext || (E[Order[j]].Voice == E[i].Voice && E[Order[j]].Note >= 0);
					if (NoteNext)
						continue;
				}
				Kept.push_back(E[i]);
			}
			E.swap(Kept);
		}
		std::set<int> Voices;
		for (size_t i = 0; i < E.size(); ++i)
			Voices.insert(E[i].Voice);
		for (std::set<int>::const_iterator v = Voices.begin(); v != Voices.end(); ++v) {
			int First = -1, Last = -1;
			bool Width = false;
			for (size_t i = 0; i < E.size(); ++i) {
				if (E[i].Voice != *v)
					continue;
				if (First < 0)
					First = static_cast<int>(i);
				Last = static_cast<int>(i);
				Width = Width || E[i].Effect == EFF_DUTY_CYCLE;
			}
			const bool Pulse = *v == HEAR_PULSE1 || *v == HEAR_PULSE2 || *v == HEAR_MMC5 || *v == HEAR_VRC6_PULSE;
			if (Pulse && !Width) {
				const int Bright = *v == HEAR_VRC6_PULSE ? 3 : 1;	// 25%: 4/16 on the VRC6
				if (E[static_cast<size_t>(First)].Effect == EFF_NONE) {
					E[static_cast<size_t>(First)].Effect = EFF_DUTY_CYCLE;
					E[static_cast<size_t>(First)].Param = Bright;
				}
				else {
					const HearEvent Before = { E[static_cast<size_t>(First)].At - 30, *v, HEAR_NONE, -1, EFF_DUTY_CYCLE, Bright };
					E.push_back(Before);
				}
			}
			if (E[static_cast<size_t>(Last)].Note != HEAR_HALT) {
				const HearEvent End = { E[static_cast<size_t>(Last)].At + 400, *v, HEAR_HALT, -1, EFF_NONE, 0 };
				E.push_back(End);
			}
		}
		for (size_t i = 0; i < E.size(); ++i)
			E[i].At += 40;
		std::stable_sort(E.begin(), E.end(), [](const HearEvent &a, const HearEvent &b) { return a.At < b.At; });
	}

private:
	HearExample &m_Out;
	std::vector<int> m_Owner;	// by event: the note it belongs to (a note's own number), -1 for none
	std::vector<bool> m_Tail;	// by event: part of a hit's fading tail
	int m_iNotes = 0;
};

std::vector<int> Notes(const char *Names)
{
	std::vector<int> Out;
	std::string Word;
	for (const char *p = Names; ; ++p) {
		if (*p == ' ' || *p == 0) {
			if (!Word.empty())
				Out.push_back(P(Word.c_str()));
			Word.clear();
			if (*p == 0)
				break;
		}
		else
			Word += *p;
	}
	return Out;
}

// Three voices holding a chord: the triangle's bass (an octave under where it's written) and the two pulses
void Chord(Score &S, int At, int Length, const char *Bass, const char *Low, const char *High, int Volume = 10)
{
	S.Note(At, HEAR_TRIANGLE, P(Bass), 15);
	S.Note(At, HEAR_PULSE1, P(Low), Volume);
	S.Note(At, HEAR_PULSE2, P(High), Volume);
	if (Length > 0) {
		S.Halt(At + Length, HEAR_TRIANGLE);
		S.Halt(At + Length, HEAR_PULSE1);
		S.Halt(At + Length, HEAR_PULSE2);
	}
}

// A beat of drums: kick on 1 and 3, snare on 2 and 4, hi-hats on the eighths between, Bars bars of Beat ms beats (the
// kicks on the noise alone where the triangle plays a bass)
void Beat(Score &S, int At, int Bars, int Beat, bool TriangleKicks = true)
{
	for (int b = 0; b < Bars * 4; ++b) {
		const int T = At + b * Beat;
		if (b % 2 == 0)
			S.Kick(T, 15, true, TriangleKicks);
		else
			S.Snare(T);
		S.Hat(T + Beat / 2);
	}
}

bool MakeExample(const std::string &Id, HearExample &Out)
{
	if (Id == "note") {
		Score S(Out, "note", "Five notes, one after another, each a step higher.");
		S.Line(0, HEAR_PULSE1, Notes("C-3 D-3 E-3 F-3 G-3"), 450, 12, 380);
		S.Finish();
	}
	else if (Id == "pitch") {
		Score S(Out, "pitch", "A low note, a middle one and a high one.");
		S.Line(0, HEAR_PULSE1, Notes("C-2 G-3 E-5"), 800, 11, 700);
		S.Finish();
	}
	else if (Id == "octave") {
		Score S(Out, "octave", "C-3, then C-4 an octave higher, then both together: the same note at two heights.");
		S.Note(0, HEAR_PULSE1, P("C-3"), 11);
		S.Note(700, HEAR_PULSE1, P("C-4"), 11);
		S.Halt(1300, HEAR_PULSE1);
		S.Note(1450, HEAR_PULSE1, P("C-3"), 10);
		S.Note(1450, HEAR_PULSE2, P("C-4"), 10);
		S.Halt(2700, HEAR_PULSE1);
		S.Halt(2700, HEAR_PULSE2);
		S.Finish();
	}
	else if (Id == "semitone") {
		Score S(Out, "semitone", "C and C#, a semitone apart, the smallest step; then C and D, a whole step (two semitones).");
		S.Line(0, HEAR_PULSE1, Notes("C-3 C#3"), 550, 11, 480);
		S.Line(1400, HEAR_PULSE1, Notes("C-3 D-3"), 550, 11, 480);
		S.Finish();
	}
	else if (Id == "sharp-flat") {
		Score S(Out, "sharp-flat", "C, C# a semitone up, and D; then D, Db (the same key as C#), and C.");
		S.Line(0, HEAR_PULSE1, Notes("C-3 C#3 D-3"), 450, 11, 380);
		S.Line(1600, HEAR_PULSE1, Notes("D-3 Db3 C-3"), 450, 11, 380);
		S.Finish();
	}
	else if (Id == "scale") {
		Score S(Out, "scale", "The C major scale up and down: do re mi fa sol la ti do.");
		const int End = S.Line(0, HEAR_PULSE1, Notes("C-3 D-3 E-3 F-3 G-3 A-3 B-3 C-4 B-3 A-3 G-3 F-3 E-3 D-3"), 240, 11, 210);
		S.Note(End, HEAR_PULSE1, P("C-3"), 11);
		S.Halt(End + 700, HEAR_PULSE1);
		S.Finish();
	}
	else if (Id == "pentatonic") {
		Score S(Out, "pentatonic", "The C major pentatonic scale up and down: five notes, C, D, E, G and A, that never clash.");
		const int End = S.Line(0, HEAR_PULSE1, Notes("C-3 D-3 E-3 G-3 A-3 C-4 A-3 G-3 E-3 D-3"), 260, 11, 230);
		S.Note(End, HEAR_PULSE1, P("C-3"), 11);
		S.Halt(End + 700, HEAR_PULSE1);
		S.Finish();
	}
	else if (Id == "home") {
		Score S(Out, "home", "A tune that stops away from home and sounds unfinished, then the same tune ending on its home "
			"note, C, which sounds finished.");
		S.Line(0, HEAR_PULSE1, Notes("E-3 D-3 C-3 D-3 E-3 F-3"), 320, 11, 290);
		S.Note(1920, HEAR_PULSE1, P("D-3"), 11);
		S.Halt(2900, HEAR_PULSE1);
		S.Note(0, HEAR_TRIANGLE, P("C-3"), 15);
		S.Note(1280, HEAR_TRIANGLE, P("G-2"), 15);
		S.Halt(2900, HEAR_TRIANGLE);
		S.Line(3300, HEAR_PULSE1, Notes("E-3 D-3 C-3 D-3 E-3 D-3"), 320, 11, 290);
		S.Note(5220, HEAR_PULSE1, P("C-3"), 11);
		S.Halt(6300, HEAR_PULSE1);
		S.Note(3300, HEAR_TRIANGLE, P("C-3"), 15);
		S.Note(4580, HEAR_TRIANGLE, P("G-2"), 15);
		S.Note(5220, HEAR_TRIANGLE, P("C-3"), 15);
		S.Halt(6300, HEAR_TRIANGLE);
		S.Finish();
	}
	else if (Id == "major-minor") {
		Score S(Out, "major-minor", "A major chord, then a minor one: only the middle note moves, a semitone down, and the "
			"mood turns darker.");
		Chord(S, 0, 1400, "C-3", "E-3", "G-3");
		Chord(S, 1600, 1400, "C-3", "Eb3", "G-3");
		Chord(S, 3200, 900, "C-3", "E-3", "G-3");
		Chord(S, 4250, 1200, "C-3", "Eb3", "G-3");
		S.Finish();
	}
	else if (Id == "interval") {
		Score S(Out, "interval", "A third (C and E), a fifth (C and G) and an octave (C and C), each as two notes one after "
			"the other, then together.");
		const char *const TOPS[] = { "E-3", "G-3", "C-4" };
		for (int i = 0; i < 3; ++i) {
			const int At = i * 1600;
			S.Note(At, HEAR_PULSE1, P("C-3"), 10);
			S.Note(At + 450, HEAR_PULSE2, P(TOPS[i]), 10);
			S.Halt(At + 1350, HEAR_PULSE1);
			S.Halt(At + 1350, HEAR_PULSE2);
		}
		S.Finish();
	}
	else if (Id == "step-leap") {
		Score S(Out, "step-leap", "First steps, each to the next note of the scale, smooth like walking; then leaps that "
			"jump over notes.");
		S.Line(0, HEAR_PULSE1, Notes("C-3 D-3 E-3 F-3 G-3"), 320, 11, 290);
		S.Line(1900, HEAR_PULSE1, Notes("C-3 A-3 F-3 C-4 G-3"), 360, 11, 320);
		S.Finish();
	}
	else if (Id == "chord") {
		Score S(Out, "chord", "A chord built a note at a time (C, then E, then G), then all three struck together.");
		S.Note(0, HEAR_TRIANGLE, P("C-3"), 15);
		S.Note(500, HEAR_PULSE1, P("E-3"), 10);
		S.Note(1000, HEAR_PULSE2, P("G-3"), 10);
		S.Halt(2200, HEAR_TRIANGLE);
		S.Halt(2200, HEAR_PULSE1);
		S.Halt(2200, HEAR_PULSE2);
		Chord(S, 2450, 1300, "C-3", "E-3", "G-3");
		S.Finish();
	}
	else if (Id == "root") {
		Score S(Out, "root", "C major with its root, C, at the bottom (solid), then with E at the bottom (lighter, as if "
			"moving on), then the root again.");
		Chord(S, 0, 1300, "C-3", "E-3", "G-3");
		Chord(S, 1450, 1300, "E-3", "G-3", "C-4");
		Chord(S, 2900, 1300, "C-3", "E-3", "G-3");
		S.Finish();
	}
	else if (Id == "third") {
		Score S(Out, "third", "A major third (C and E, four semitones), bright; then a minor third (C and Eb, three), "
			"darker.");
		S.Note(0, HEAR_PULSE1, P("C-3"), 10);
		S.Note(0, HEAR_PULSE2, P("E-3"), 10);
		S.Note(1500, HEAR_PULSE1, P("C-3"), 10);
		S.Note(1500, HEAR_PULSE2, P("Eb3"), 10);
		S.Halt(1300, HEAR_PULSE1);
		S.Halt(1300, HEAR_PULSE2);
		S.Halt(2800, HEAR_PULSE1);
		S.Halt(2800, HEAR_PULSE2);
		S.Finish();
	}
	else if (Id == "fifth") {
		Score S(Out, "fifth", "A fifth, C and G: open and strong; then a power chord, the fifth with a bass and the octave.");
		S.Note(0, HEAR_PULSE1, P("C-3"), 10);
		S.Note(0, HEAR_PULSE2, P("G-3"), 10);
		S.Halt(1300, HEAR_PULSE1);
		S.Halt(1300, HEAR_PULSE2);
		Chord(S, 1500, 1400, "C-3", "G-3", "C-4");
		S.Finish();
	}
	else if (Id == "seventh") {
		Score S(Out, "seventh", "C major, then C major seventh (dreamy), then G7, which leans toward home, landing on C.");
		Chord(S, 0, 1100, "C-3", "E-3", "G-3");
		Chord(S, 1250, 1100, "C-3", "E-3", "B-3");
		Chord(S, 2500, 1100, "G-2", "F-3", "B-3");
		Chord(S, 3750, 1400, "C-3", "E-3", "C-4");
		S.Finish();
	}
	else if (Id == "roles") {
		Score S(Out, "roles", "C (home), F (away), G7 (tension) and C again (home): a little trip away from home and back.");
		Chord(S, 0, 1100, "C-3", "E-3", "G-3");
		Chord(S, 1250, 1100, "F-2", "A-3", "C-4");
		Chord(S, 2500, 1100, "G-2", "F-3", "B-3");
		Chord(S, 3750, 1500, "C-3", "E-3", "C-4");
		S.Finish();
	}
	else if (Id == "borrowed") {
		Score S(Out, "borrowed", "C, F, then F minor, borrowed from C minor (a darker shade of the same chord), and home to C.");
		Chord(S, 0, 1100, "C-3", "E-3", "G-3");
		Chord(S, 1250, 1100, "F-2", "A-3", "C-4");
		Chord(S, 2500, 1300, "F-2", "Ab3", "C-4");
		Chord(S, 3950, 1500, "C-3", "E-3", "G-3");
		S.Finish();
	}
	else if (Id == "progression") {
		Score S(Out, "progression", "Four chords, one after another: C, G, A minor and F, the four chords of countless songs, "
			"then home to C.");
		Chord(S, 0, 900, "C-3", "E-3", "G-3");
		Chord(S, 1000, 900, "G-2", "D-3", "B-3");
		Chord(S, 2000, 900, "A-2", "E-3", "C-4");
		Chord(S, 3000, 900, "F-2", "F-3", "A-3");
		Chord(S, 4000, 1300, "C-3", "E-3", "G-3");
		S.Finish();
	}
	else if (Id == "melody") {
		Score S(Out, "melody", "A short melody over a bass line: the tune is the part you'd hum.");
		const int Ms[] = { 250, 250, 500, 250, 250, 500, 250, 250, 250, 250, 1000 };
		const std::vector<int> Tune = Notes("E-3 G-3 A-3 G-3 E-3 D-3 C-3 D-3 E-3 G-3 E-3");
		int At = 0;
		for (size_t i = 0; i < Tune.size(); ++i) {
			S.Note(At, HEAR_PULSE1, Tune[i], 12);
			S.Halt(At + Ms[i] - 40, HEAR_PULSE1);
			At += Ms[i];
		}
		S.Note(0, HEAR_TRIANGLE, P("C-3"), 15);
		S.Note(1000, HEAR_TRIANGLE, P("A-2"), 15);
		S.Note(2000, HEAR_TRIANGLE, P("F-2"), 15);
		S.Note(3000, HEAR_TRIANGLE, P("C-3"), 15);
		S.Halt(At, HEAR_TRIANGLE);
		S.Finish();
	}
	else if (Id == "hook") {
		Score S(Out, "hook", "A catchy two-bar hook, then the same hook a step higher, then once more, landing home.");
		const int Ms[] = { 250, 250, 250, 250, 750 };
		const char *const HOOKS[] = { "G-3 E-3 G-3 A-3 G-3", "A-3 F-3 A-3 B-3 A-3", "G-3 E-3 G-3 A-3 C-4" };
		int At = 0;
		for (int h = 0; h < 3; ++h) {
			const std::vector<int> Hook = Notes(HOOKS[h]);
			for (size_t i = 0; i < Hook.size(); ++i) {
				S.Note(At, HEAR_PULSE1, Hook[i], 12);
				S.Halt(At + Ms[i] - 50, HEAR_PULSE1);
				At += Ms[i];
			}
			At += 250;
		}
		S.Finish();
	}
	else if (Id == "phrase") {
		Score S(Out, "phrase", "Two phrases, each ending on a long note with a breath after it, like a question and its "
			"answer.");
		int At = S.Line(0, HEAR_PULSE1, Notes("C-3 D-3 E-3 G-3 E-3 D-3"), 250, 12, 220);
		S.Note(At, HEAR_PULSE1, P("E-3"), 12);
		S.Halt(At + 750, HEAR_PULSE1);
		At += 1250;
		At = S.Line(At, HEAR_PULSE1, Notes("C-3 D-3 E-3 G-3 A-3 G-3"), 250, 12, 220);
		S.Note(At, HEAR_PULSE1, P("C-3"), 12);
		S.Halt(At + 900, HEAR_PULSE1);
		S.Finish();
	}
	else if (Id == "rest") {
		Score S(Out, "rest", "Notes without a break, then the same notes in groups with rests between them, which let the "
			"tune breathe.");
		S.Line(0, HEAR_PULSE1, Notes("C-3 E-3 G-3 E-3 C-3 E-3 G-3 C-4"), 250, 11);
		S.Line(2400, HEAR_PULSE1, Notes("C-3 E-3 G-3"), 250, 11, 220);
		S.Line(3650, HEAR_PULSE1, Notes("E-3 G-3 C-4"), 250, 11, 220);
		S.Finish();
	}
	else if (Id == "rhythm") {
		Score S(Out, "rhythm", "The same five notes evenly, then with long and short notes: another tune from the same "
			"notes.");
		S.Line(0, HEAR_PULSE1, Notes("E-3 E-3 F-3 G-3 G-3"), 400, 11, 340);
		const int Ms[] = { 600, 200, 400, 300, 700 };
		const std::vector<int> Tune = Notes("E-3 E-3 F-3 G-3 G-3");
		int At = 2400;
		for (size_t i = 0; i < Tune.size(); ++i) {
			S.Note(At, HEAR_PULSE1, Tune[i], 11);
			S.Halt(At + Ms[i] - 50, HEAR_PULSE1);
			At += Ms[i];
		}
		S.Finish();
	}
	else if (Id == "beat") {
		Score S(Out, "beat", "Eight beats, the steady pulse to tap your foot to; the first of every four is lower and "
			"stronger.");
		for (int b = 0; b < 8; ++b) {
			if (b % 4 == 0)
				S.Kick(b * 500);
			else
				S.Hit(b * 500, HEAR_NOISE, 0xC, 10, 2, 40);
		}
		S.Finish();
	}
	else if (Id == "bar") {
		Score S(Out, "bar", "Three bars of four beats: a low drum on each bar's first beat, a tick on the others.");
		for (int b = 0; b < 12; ++b) {
			if (b % 4 == 0)
				S.Kick(b * 450);
			else
				S.Hit(b * 450, HEAR_NOISE, 0xC, 9, 2, 40);
		}
		S.Finish();
	}
	else if (Id == "meter") {
		Score S(Out, "meter", "Two bars of 4/4 (ONE two three four), two of 3/4, a waltz (ONE two three), then two of 6/8 "
			"(ONE two three FOUR five six).");
		int At = 0;
		for (int b = 0; b < 8; ++b, At += 450) {
			if (b % 4 == 0)
				S.Kick(At);
			else
				S.Hit(At, HEAR_NOISE, 0xC, 9, 2, 40);
		}
		At += 300;
		for (int b = 0; b < 6; ++b, At += 450) {
			if (b % 3 == 0)
				S.Kick(At);
			else
				S.Hit(At, HEAR_NOISE, 0xC, 9, 2, 40);
		}
		At += 300;
		for (int b = 0; b < 12; ++b, At += 200) {
			if (b % 6 == 0)
				S.Kick(At);
			else if (b % 3 == 0)
				S.Hit(At, HEAR_NOISE, 0xA, 11, 3, 40);
			else
				S.Hit(At, HEAR_NOISE, 0xD, 7, 1, 40);
		}
		S.Finish();
	}
	else if (Id == "tempo") {
		Score S(Out, "tempo", "Four beats at 80 BPM, calm, then eight at 160 BPM, a chase: the same pattern, faster.");
		const std::vector<int> Tune = Notes("C-3 E-3 G-3 E-3 C-3 E-3 G-3 E-3");
		for (int b = 0; b < 4; ++b) {
			S.Hit(b * 750, HEAR_NOISE, 0xC, 9, 2, 40);
			S.Note(b * 750, HEAR_PULSE1, Tune[static_cast<size_t>(b)], 10);
			S.Halt(b * 750 + 600, HEAR_PULSE1);
		}
		for (int b = 0; b < 8; ++b) {
			const int At = 3300 + b * 375;
			S.Hit(At, HEAR_NOISE, 0xC, 9, 2, 40);
			S.Note(At, HEAR_PULSE1, Tune[static_cast<size_t>(b)], 10);
			S.Halt(At + 300, HEAR_PULSE1);
		}
		S.Finish();
	}
	else if (Id == "swing") {
		Score S(Out, "swing", "Eighth notes played straight, even like a ticking clock; then with swing, long-short, "
			"long-short, like a skipping walk.");
		const std::vector<int> Tune = Notes("C-3 E-3 G-3 E-3 A-3 G-3 E-3 D-3");
		for (int i = 0; i < 16; ++i) {
			const int Pair = i / 2, Late = i % 2;
			const int Straight = Pair * 500 + Late * 250;
			const int Swung = 4400 + Pair * 500 + Late * 333;
			for (int Way = 0; Way < 2; ++Way) {
				const int At = Way == 0 ? Straight : Swung;
				S.Note(At, HEAR_PULSE1, Tune[static_cast<size_t>(i % 8)], Late ? 9 : 12);
				S.Halt(At + (Late ? 130 : (Way == 0 ? 200 : 290)), HEAR_PULSE1);
				S.Hit(At, HEAR_NOISE, Late ? 0xD : 0xC, Late ? 6 : 9, 1, 40);
			}
		}
		S.Finish();
	}
	else if (Id == "syncopation") {
		Score S(Out, "syncopation", "A rhythm right on the beats, then a syncopated one with notes between them (three, three, "
			"two), which pushes and bounces.");
		for (int Bar = 0; Bar < 2; ++Bar)
			for (int b = 0; b < 4; ++b) {
				const int At = Bar * 2000 + b * 500;
				S.Note(At, HEAR_TRIANGLE, P(b < 2 ? "C-3" : "G-2"), 15);
				S.Halt(At + 300, HEAR_TRIANGLE);
				S.Hat(At);
			}
		const int SYNC[] = { 0, 375, 750 };
		for (int Bar = 0; Bar < 4; ++Bar)
			for (int n = 0; n < 3; ++n) {
				const int At = 4400 + Bar * 1000 + SYNC[n];
				S.Note(At, HEAR_TRIANGLE, P(Bar < 2 ? "C-3" : "G-2"), 15);
				S.Halt(At + 250, HEAR_TRIANGLE);
				S.Hat(At);
			}
		S.Finish();
	}
	else if (Id == "accent") {
		Score S(Out, "accent", "Eight even notes, then the same with some of them louder (accented): the rhythm comes alive.");
		const std::vector<int> Tune = Notes("C-3 C-3 G-3 C-3 E-3 C-3 G-3 C-3");
		const bool LOUD[] = { true, false, false, true, false, false, true, false };
		for (int i = 0; i < 8; ++i) {
			S.Note(i * 250, HEAR_PULSE1, Tune[static_cast<size_t>(i)], 7);
			S.Halt(i * 250 + 200, HEAR_PULSE1);
			S.Note(2400 + i * 250, HEAR_PULSE1, Tune[static_cast<size_t>(i)], LOUD[i] ? 15 : 5);
			S.Halt(2400 + i * 250 + 200, HEAR_PULSE1);
		}
		S.Finish();
	}
	else if (Id == "dynamics") {
		Score S(Out, "dynamics", "Notes growing from soft to loud, then fading back: dynamics.");
		const int VOLUMES[] = { 2, 4, 6, 8, 10, 12, 14, 15, 13, 11, 9, 7, 5, 3, 2 };
		const std::vector<int> Tune = Notes("C-3 D-3 E-3 F-3 G-3 A-3 B-3 C-4 B-3 A-3 G-3 F-3 E-3 D-3 C-3");
		for (int i = 0; i < 15; ++i) {
			S.Note(i * 280, HEAR_PULSE1, Tune[static_cast<size_t>(i)], VOLUMES[i]);
			S.Halt(i * 280 + 240, HEAR_PULSE1);
		}
		S.Finish();
	}
	else if (Id == "staccato-legato") {
		Score S(Out, "staccato-legato", "A tune played staccato, short and bouncy, then legato, each note running into the "
			"next.");
		S.Line(0, HEAR_PULSE1, Notes("C-3 E-3 G-3 E-3 F-3 A-3 G-3 E-3"), 280, 12, 80);
		S.Line(2600, HEAR_PULSE1, Notes("C-3 E-3 G-3 E-3 F-3 A-3 G-3 E-3"), 280, 12);
		S.Finish();
	}
	else if (Id == "bass") {
		Score S(Out, "bass", "A bass line on the triangle, low and round, playing the chords' roots under soft chords.");
		const int Ms[] = { 375, 125, 500, 375, 125, 500, 375, 125, 500, 500, 500 };
		const std::vector<int> Line = Notes("C-3 C-3 G-2 A-2 A-2 E-2 F-2 F-2 C-3 G-2 C-3");
		int At = 0;
		for (size_t i = 0; i < Line.size(); ++i) {
			S.Note(At, HEAR_TRIANGLE, Line[i], 15);
			S.Halt(At + Ms[i] - 40, HEAR_TRIANGLE);
			At += Ms[i];
		}
		S.Note(0, HEAR_PULSE1, P("E-3"), 5);
		S.Note(0, HEAR_PULSE2, P("G-3"), 5);
		S.Note(1000, HEAR_PULSE1, P("E-3"), 5);
		S.Note(1000, HEAR_PULSE2, P("A-3"), 5);
		S.Note(2000, HEAR_PULSE1, P("F-3"), 5);
		S.Note(2000, HEAR_PULSE2, P("A-3"), 5);
		S.Note(3000, HEAR_PULSE1, P("E-3"), 5);
		S.Note(3000, HEAR_PULSE2, P("G-3"), 5);
		S.Halt(At, HEAR_PULSE1);
		S.Halt(At, HEAR_PULSE2);
		S.Finish();
	}
	else if (Id == "harmony") {
		Score S(Out, "harmony", "A melody alone, then with a harmony a third under it: fuller, with the tune still on top.");
		S.Line(0, HEAR_PULSE1, Notes("E-3 F-3 G-3 E-3 D-3 E-3"), 320, 12, 290);
		S.Line(2300, HEAR_PULSE1, Notes("E-3 F-3 G-3 E-3 D-3 E-3"), 320, 12, 290);
		S.Line(2300, HEAR_PULSE2, Notes("C-3 D-3 E-3 C-3 B-2 C-3"), 320, 7, 290);
		S.Finish();
	}
	else if (Id == "counter-melody") {
		Score S(Out, "counter-melody", "A slow melody, and under it a counter-melody that moves while the melody holds its "
			"notes.");
		S.Line(0, HEAR_PULSE1, Notes("C-4 B-3 A-3 G-3"), 1000, 12, 900);
		const char *const ANSWERS[] = { "E-3 F-3", "D-3 E-3", "C-3 D-3", "B-2 C-3" };
		for (int i = 0; i < 4; ++i)
			S.Line(i * 1000 + 450, HEAR_PULSE2, Notes(ANSWERS[i]), 250, 7, 220);
		S.Finish();
	}
	else if (Id == "arpeggio") {
		Score S(Out, "arpeggio", "A chord's notes one after another (a broken chord), then so fast on one channel that they "
			"blur into a chord: major (047), then minor (037).");
		S.Line(0, HEAR_PULSE1, Notes("C-3 E-3 G-3 C-4 G-3 E-3 C-3"), 220, 11, 200);
		S.Note(1800, HEAR_PULSE1, P("C-3"), 11, EFF_ARPEGGIO, 0x47);
		S.Note(3200, HEAR_PULSE1, P("C-3"), 11, EFF_ARPEGGIO, 0x37);
		S.Halt(4600, HEAR_PULSE1);
		S.Finish();
	}
	else if (Id == "drums") {
		Score S(Out, "drums", "A basic beat: the kick on beats one and three, the snare on two and four, hi-hats ticking in "
			"between.");
		Beat(S, 0, 2, 500);
		S.Finish();
	}
	else if (Id == "kick") {
		Score S(Out, "kick", "Kick drums: a low thump of noise with a short triangle note dropping fast.");
		const int AT[] = { 0, 500, 1000, 1250, 1500 };
		for (int i = 0; i < 5; ++i)
			S.Kick(AT[i]);
		S.Finish();
	}
	else if (Id == "snare") {
		Score S(Out, "snare", "Snare hits: a burst of noise in its middle pitches, dying away fast.");
		const int AT[] = { 0, 500, 1000, 1250, 1500 };
		for (int i = 0; i < 5; ++i)
			S.Snare(AT[i]);
		S.Finish();
	}
	else if (Id == "hi-hat") {
		Score S(Out, "hi-hat", "Closed hi-hats ticking on every eighth note, then two open ones.");
		for (int i = 0; i < 8; ++i)
			S.Hat(i * 250, i % 2 == 0 ? 9 : 6);
		S.Hit(2100, HEAR_NOISE, 0xC, 9, 6, 50);
		S.Hit(2600, HEAR_NOISE, 0xC, 9, 6, 50);
		S.Finish();
	}
	else if (Id == "crash") {
		Score S(Out, "crash", "A crash cymbal with a kick under it, ringing and fading; then another.");
		S.Kick(0, 15, false, true);
		S.Hit(0, HEAR_NOISE, 0xE, 13, 13, 100);
		S.Kick(1800, 15, false, true);
		S.Hit(1800, HEAR_NOISE, 0xE, 13, 13, 100);
		S.Finish();
	}
	else if (Id == "tom") {
		Score S(Out, "tom", "Toms from high to low, a quick run down, the way a fill plays them.");
		const std::vector<int> Toms = Notes("A-3 F-3 D-3 A-2");
		for (int Run = 0; Run < 2; ++Run)
			for (size_t i = 0; i < Toms.size(); ++i) {
				const int At = Run * 1300 + static_cast<int>(i) * 180;
				S.Note(At, HEAR_TRIANGLE, Toms[i], 15, EFF_PORTA_DOWN, 0x20);
				S.Halt(At + 150, HEAR_TRIANGLE);
			}
		S.Finish();
	}
	else if (Id == "fill") {
		Score S(Out, "fill", "A bar of a beat, then a fill (snare hits and falling toms) leading into a crash on the next "
			"bar's first beat.");
		Beat(S, 0, 1, 500);
		for (int i = 0; i < 4; ++i)
			S.Snare(2000 + i * 125, 9 + i);
		const std::vector<int> Toms = Notes("A-3 F-3 D-3 A-2");
		for (size_t i = 0; i < Toms.size(); ++i) {
			const int At = 2500 + static_cast<int>(i) * 250;
			S.Note(At, HEAR_TRIANGLE, Toms[i], 15, EFF_PORTA_DOWN, 0x20);
			S.Halt(At + 200, HEAR_TRIANGLE);
			S.Snare(At + 125, 10);
		}
		S.Kick(3500, 15, false, true);
		S.Hit(3500, HEAR_NOISE, 0xE, 13, 12, 100);
		S.Finish();
	}
	else if (Id == "groove") {
		Score S(Out, "groove", "Two bars of a groove: drums and a bass line locked together.");
		Beat(S, 0, 2, 500, false);
		const int Ms[] = { 375, 125, 250, 250, 500, 250, 250, 375, 125, 250, 250, 500, 500 };
		const std::vector<int> Line = Notes("C-3 C-3 G-2 C-3 A-2 C-3 D-3 F-2 F-2 A-2 C-3 G-2 C-3");
		int At = 0;
		for (size_t i = 0; i < Line.size() && At < 4000; ++i) {
			S.Note(At, HEAR_TRIANGLE, Line[i], 15);
			S.Halt(At + Ms[i] - 50, HEAR_TRIANGLE);
			At += Ms[i];
		}
		S.Finish();
	}
	else if (Id == "repetition") {
		Score S(Out, "repetition", "A phrase, the same phrase again, then once more with a new ending: repetition with a "
			"variation.");
		int At = 0;
		const char *const PHRASES[] = { "C-3 E-3 G-3 E-3 D-3", "C-3 E-3 G-3 E-3 D-3", "C-3 E-3 G-3 A-3 C-4" };
		for (int p = 0; p < 3; ++p) {
			const std::vector<int> Tune = Notes(PHRASES[p]);
			for (size_t i = 0; i < Tune.size(); ++i) {
				const int Long = i + 1 == Tune.size() ? 600 : 250;
				S.Note(At, HEAR_PULSE1, Tune[i], 12);
				S.Halt(At + Long - 40, HEAR_PULSE1);
				At += Long;
			}
			At += 250;
		}
		S.Finish();
	}
	else if (Id == "tension") {
		Score S(Out, "tension", "G7, full of tension, leaning toward home; then C, the home chord: the release.");
		Chord(S, 0, 1500, "G-2", "F-3", "B-3");
		Chord(S, 1650, 1700, "C-3", "E-3", "C-4");
		Chord(S, 3600, 1100, "G-2", "F-3", "B-3");
		Chord(S, 4800, 1500, "C-3", "E-3", "C-4");
		S.Finish();
	}
	else if (Id == "clash") {
		Score S(Out, "clash", "E and F together, a semitone apart: a sour clash; then E and G, which blend.");
		S.Note(0, HEAR_PULSE1, P("E-3"), 10);
		S.Note(0, HEAR_PULSE2, P("F-3"), 10);
		S.Halt(1400, HEAR_PULSE1);
		S.Halt(1400, HEAR_PULSE2);
		S.Note(1600, HEAR_PULSE1, P("E-3"), 10);
		S.Note(1600, HEAR_PULSE2, P("G-3"), 10);
		S.Halt(3000, HEAR_PULSE1);
		S.Halt(3000, HEAR_PULSE2);
		S.Finish();
	}
	else if (Id == "layers") {
		Score S(Out, "layers", "Drums alone, then a bass joins, then chords, then the melody: four layers, one at a time.");
		const int BarMs = 1600;
		for (int Bar = 0; Bar < 4; ++Bar) {
			const int Start = Bar * BarMs;
			for (int b = 0; b < 4; ++b) {
				if (b % 2 == 0)
					S.Hit(Start + b * 400, HEAR_NOISE, 0x3, 13, 3, 40);
				else
					S.Snare(Start + b * 400, 11);
				S.Hat(Start + b * 400 + 200, 6);
			}
			if (Bar >= 1) {
				S.Note(Start, HEAR_TRIANGLE, P(Bar % 2 ? "C-3" : "A-2"), 15);
				S.Halt(Start + 300, HEAR_TRIANGLE);
				S.Note(Start + 800, HEAR_TRIANGLE, P(Bar % 2 ? "G-2" : "E-2"), 15);
				S.Halt(Start + 1100, HEAR_TRIANGLE);
			}
			if (Bar >= 2) {
				S.Note(Start, HEAR_PULSE2, P(Bar % 2 ? "C-3" : "A-2"), 6, EFF_ARPEGGIO, Bar % 2 ? 0x47 : 0x37);
				S.Halt(Start + 1500, HEAR_PULSE2);
			}
			if (Bar >= 3)
				S.Line(Start, HEAR_PULSE1, Notes("E-4 D-4 C-4 A-3"), 400, 12, 360);
		}
		S.Note(4 * BarMs, HEAR_PULSE2, P("C-3"), 6, EFF_ARPEGGIO, 0x00);
		S.Halt(4 * BarMs + 50, HEAR_PULSE2);
		S.Finish();
	}
	else if (Id == "echo") {
		Score S(Out, "echo", "A tune alone, then with an echo: the same notes an eighth later and softer on a second "
			"channel.");
		S.Line(0, HEAR_PULSE1, Notes("C-3 E-3 G-3 C-4 G-3 E-3"), 300, 12, 260);
		S.Line(2200, HEAR_PULSE1, Notes("C-3 E-3 G-3 C-4 G-3 E-3"), 300, 12, 260);
		S.Line(2450, HEAR_PULSE2, Notes("C-3 E-3 G-3 C-4 G-3 E-3"), 300, 5, 260);
		S.Finish();
	}
	else if (Id == "panning") {
		Score S(Out, "panning", "A note on the left, in the middle and on the right, then a tune hopping between the sides: "
			"stereo, this FamiTracker's own (8xx).");
		S.Note(0, HEAR_PULSE1, P("E-3"), 11, EFF_PAN, 0x00);
		S.Note(900, HEAR_PULSE1, P("E-3"), 11, EFF_PAN, 0x80);
		S.Note(1800, HEAR_PULSE1, P("E-3"), 11, EFF_PAN, 0xFF);
		S.Halt(2600, HEAR_PULSE1);
		const std::vector<int> Tune = Notes("C-3 E-3 G-3 C-4 G-3 E-3");
		for (int i = 0; i < 6; ++i) {
			S.Note(2900 + i * 300, HEAR_PULSE1, Tune[static_cast<size_t>(i)], 11, EFF_PAN, i % 2 ? 0xE0 : 0x20);
			S.Halt(2900 + i * 300 + 260, HEAR_PULSE1);
		}
		S.Finish();
	}
	else if (Id == "mix") {
		Score S(Out, "mix", "A tune buried under loud chords, then the same balanced, with the tune clear on top.");
		for (int Way = 0; Way < 2; ++Way) {
			const int Start = Way * 2800;
			S.Line(Start, HEAR_PULSE1, Notes("E-4 D-4 C-4 D-4 E-4 E-4"), 400, Way ? 13 : 5, 360);
			S.Note(Start, HEAR_PULSE2, P("C-3"), Way ? 5 : 14, EFF_ARPEGGIO, 0x47);
			S.Halt(Start + 2400, HEAR_PULSE2);
			S.Note(Start, HEAR_TRIANGLE, P("C-3"), 15);
			S.Halt(Start + 2400, HEAR_TRIANGLE);
		}
		S.Note(5600, HEAR_PULSE2, P("C-3"), 0, EFF_ARPEGGIO, 0x00);
		S.Halt(5650, HEAR_PULSE2);
		S.Finish();
	}
	else if (Id == "timbre") {
		Score S(Out, "timbre", "The same tune on a pulse, then on the triangle: the same notes in another color.");
		S.Line(0, HEAR_PULSE1, Notes("C-3 E-3 G-3 E-3 C-4"), 300, 11, 270);
		S.Line(1900, HEAR_TRIANGLE, Notes("C-4 E-4 G-4 E-4 C-5"), 300, 15, 270);
		S.Finish();
	}
	else if (Id == "envelope") {
		Score S(Out, "envelope", "Plucks that start at once and die away quickly, then a pad that swells in slowly, holds and "
			"fades.");
		const std::vector<int> Plucks = Notes("C-3 E-3 G-3 C-4 G-3 E-3");
		for (size_t i = 0; i < Plucks.size(); ++i)
			S.Hit(static_cast<int>(i) * 300, HEAR_PULSE1, Plucks[i], 14, 6, 45);
		const int Pad = 2200;
		S.Note(Pad, HEAR_PULSE1, P("E-3"), 1);
		S.Note(Pad, HEAR_PULSE2, P("G-3"), 1);
		const int SWELL[] = { 2, 3, 4, 5, 6, 7, 8, 9 };
		for (int i = 0; i < 8; ++i) {
			S.Set(Pad + 100 + i * 100, HEAR_PULSE1, SWELL[i]);
			S.Set(Pad + 100 + i * 100, HEAR_PULSE2, SWELL[i]);
		}
		for (int i = 0; i < 8; ++i) {
			S.Set(Pad + 1600 + i * 110, HEAR_PULSE1, 8 - i);
			S.Set(Pad + 1600 + i * 110, HEAR_PULSE2, 8 - i);
		}
		S.Halt(Pad + 2500, HEAR_PULSE1);
		S.Halt(Pad + 2500, HEAR_PULSE2);
		S.Finish();
	}
	else if (Id == "vibrato") {
		Score S(Out, "vibrato", "A long note held plain, then the same note with vibrato coming in after it starts, like a "
			"singer.");
		S.Note(0, HEAR_PULSE1, P("E-3"), 11);
		S.Halt(1300, HEAR_PULSE1);
		S.Note(1500, HEAR_PULSE1, P("E-3"), 11);
		S.Set(1950, HEAR_PULSE1, -1, EFF_VIBRATO, 0x45);
		S.Halt(3700, HEAR_PULSE1);
		S.Finish();
	}
	else if (Id == "slide") {
		Score S(Out, "slide", "A scoop up into a note, a fall at the end of a phrase, then notes gliding into each other "
			"(portamento).");
		S.Note(0, HEAR_PULSE1, P("A#2"), 11, EFF_SLIDE_UP, 0x32);
		S.Halt(900, HEAR_PULSE1);
		S.Line(1100, HEAR_PULSE1, Notes("E-3 D-3"), 300, 11, 270);
		S.Note(1700, HEAR_PULSE1, P("C-3"), 11);
		S.Set(2100, HEAR_PULSE1, -1, EFF_SLIDE_DOWN, 0x45);
		S.Halt(2700, HEAR_PULSE1);
		S.Note(2900, HEAR_PULSE1, P("C-3"), 11, EFF_PORTAMENTO, 0x0A);
		S.Note(3250, HEAR_PULSE1, P("E-3"), 11);
		S.Note(3600, HEAR_PULSE1, P("G-3"), 11);
		S.Note(3950, HEAR_PULSE1, P("E-3"), 11);
		S.Note(4300, HEAR_PULSE1, P("C-3"), 11);
		S.Set(4800, HEAR_PULSE1, -1, EFF_PORTAMENTO, 0x00);
		S.Halt(4850, HEAR_PULSE1);
		S.Finish();
	}
	else if (Id == "detune") {
		Score S(Out, "detune", "One pulse alone, then two on the same note, one a touch sharp (P82): they shimmer and sound "
			"thicker.");
		S.Note(0, HEAR_PULSE1, P("C-3"), 10);
		S.Halt(1400, HEAR_PULSE1);
		S.Note(1600, HEAR_PULSE1, P("C-3"), 9);
		S.Note(1600, HEAR_PULSE2, P("C-3"), 9, EFF_PITCH, 0x82);
		S.Halt(3600, HEAR_PULSE1);
		S.Halt(3600, HEAR_PULSE2);
		S.Finish();
	}
	else if (Id == "pulse-width") {
		Score S(Out, "pulse-width", "The same notes at each pulse width: 12.5% (thin), 25% (bright), 50% (round, a square), "
			"then 75%.");
		for (int w = 0; w < 4; ++w) {
			const std::vector<int> Tune = Notes("C-3 E-3 G-3");
			const int Start = w * 1150;
			for (size_t i = 0; i < Tune.size(); ++i) {
				S.Note(Start + static_cast<int>(i) * 300, HEAR_PULSE1, Tune[i], 11, i == 0 ? EFF_DUTY_CYCLE : EFF_NONE, w);
				S.Halt(Start + static_cast<int>(i) * 300 + (i == 2 ? 500 : 270), HEAR_PULSE1);
			}
		}
		S.Finish();
	}
	else if (Id == "pulse") {
		Score S(Out, "pulse", "A tune on a pulse channel, the NES's lead voice.");
		S.Line(0, HEAR_PULSE1, Notes("E-3 G-3 A-3 G-3 E-3 D-3 C-3 D-3 E-3"), 300, 12, 270);
		S.Finish();
	}
	else if (Id == "triangle") {
		Score S(Out, "triangle", "A bass line on the triangle, then a tune on it: soft and round, always at full volume.");
		S.Line(0, HEAR_TRIANGLE, Notes("C-3 G-2 A-2 E-2 F-2 C-3 G-2 C-3"), 350, 15, 300);
		S.Line(3000, HEAR_TRIANGLE, Notes("E-4 G-4 A-4 G-4 E-4 C-4"), 300, 15, 270);
		S.Finish();
	}
	else if (Id == "noise") {
		Score S(Out, "noise", "The noise from low to high, then its metallic mode (V01).");
		for (int p = 0; p < 16; ++p) {
			S.Note(p * 160, HEAR_NOISE, p, 9);
			S.Halt(p * 160 + 130, HEAR_NOISE);
		}
		const int METAL[] = { 0x4, 0x8, 0xC };
		for (int i = 0; i < 3; ++i) {
			S.Note(2800 + i * 450, HEAR_NOISE, METAL[i], 8, EFF_DUTY_CYCLE, 1);
			S.Halt(2800 + i * 450 + 380, HEAR_NOISE);
		}
		S.Note(4250, HEAR_NOISE, HEAR_NONE, -1, EFF_DUTY_CYCLE, 0);
		S.Finish();
	}
	else if (Id == "sawtooth") {
		Score S(Out, "sawtooth", "A bass line on the VRC6's sawtooth, then a tune on it: bright and buzzy.", "VRC6");
		S.Line(0, HEAR_SAWTOOTH, Notes("C-2 C-2 G-1 A-1 F-1 G-1"), 350, 11, 300);
		S.Line(2400, HEAR_SAWTOOTH, Notes("E-3 G-3 A-3 G-3 E-3 C-3"), 300, 10, 270);
		S.Finish();
	}
	else if (Id == "vrc6") {
		Score S(Out, "vrc6", "The VRC6's pulse at three of its eight widths, then its sawtooth.", "VRC6");
		const int WIDTHS[] = { 1, 3, 7 };
		for (int w = 0; w < 3; ++w) {
			S.Note(w * 800, HEAR_VRC6_PULSE, P("E-3"), 10, EFF_DUTY_CYCLE, WIDTHS[w]);
			S.Halt(w * 800 + 700, HEAR_VRC6_PULSE);
		}
		S.Line(2500, HEAR_SAWTOOTH, Notes("C-3 E-3 G-3 C-4"), 300, 10, 270);
		S.Finish();
	}
	else if (Id == "vrc6-plus") {
		Score S(Out, "vrc6-plus", "The soft layer's first channel, Soft 1, with a plain drawn wave: a softer voice to put under "
			"the VRC6.", "VRC6 Plus");
		S.Line(0, HEAR_N163, Notes("C-4 E-4 G-4 E-4 F-4 D-4 C-4"), 380, 12, 350);
		S.Finish();
	}
	else if (Id == "vrc7") {
		Score S(Out, "vrc7", "A tune on the VRC7's first FM channel.", "VRC7");
		S.Line(0, HEAR_VRC7, Notes("C-3 E-3 G-3 C-4 G-3 E-3 C-3"), 350, 13, 320);
		S.Finish();
	}
	else if (Id == "fds") {
		Score S(Out, "fds", "A tune on the FDS's channel, with its default drawn wave.", "FDS");
		S.Line(0, HEAR_FDS, Notes("C-5 E-5 G-5 C-6 G-5 E-5 C-5"), 350, 13, 320);
		S.Finish();
	}
	else if (Id == "n163") {
		Score S(Out, "n163", "A tune on an N163 channel with a plain drawn wave (a triangle).", "N163");
		S.Line(0, HEAR_N163, Notes("C-4 E-4 G-4 C-5 G-4 E-4 C-4"), 350, 13, 320);
		S.Finish();
	}
	else if (Id == "mmc5") {
		Score S(Out, "mmc5", "A tune on the MMC5's first pulse, which sounds just like the 2A03's, over a 2A03 pulse "
			"holding a note.", "MMC5");
		S.Line(0, HEAR_MMC5, Notes("E-3 G-3 A-3 G-3 E-3 C-3"), 330, 12, 300);
		S.Note(0, HEAR_PULSE1, P("C-3"), 6);
		S.Halt(1980, HEAR_PULSE1);
		S.Finish();
	}
	else if (Id == "5b") {
		Score S(Out, "5b", "The 5B's square, then its noise, then its envelope's buzz.", "5B");
		S.Line(0, HEAR_5B, Notes("C-4 E-4 G-4"), 350, 12, 320);
		S.Note(1200, HEAR_5B, P("C-4"), 12, EFF_DUTY_CYCLE, 2);
		S.Halt(1500, HEAR_5B);
		S.Note(1650, HEAR_5B, P("C-4"), 12);
		S.Halt(1950, HEAR_5B);
		S.Set(2200, HEAR_5B, -1, EFF_DUTY_CYCLE, 4);
		S.Note(2300, HEAR_5B, P("C-3"), 12, EFF_SUNSOFT_ENV_TYPE, 0xC8);
		S.Halt(3300, HEAR_5B);
		S.Set(3400, HEAR_5B, -1, EFF_DUTY_CYCLE, 1);
		S.Finish();
	}
	else if (Id == "2a03") {
		Score S(Out, "2a03", "The 2A03's channels one at a time, the two pulses, the triangle and the noise, then all "
			"together.");
		S.Line(0, HEAR_PULSE1, Notes("E-3 G-3"), 300, 11, 270);
		S.Line(700, HEAR_PULSE2, Notes("C-3 E-3"), 300, 9, 270);
		S.Line(1400, HEAR_TRIANGLE, Notes("C-3 G-2"), 300, 15, 270);
		S.Hat(2100);
		S.Snare(2400);
		Beat(S, 3000, 1, 450);
		S.Line(3000, HEAR_PULSE1, Notes("E-4 D-4 C-4 G-3"), 450, 11, 420);
		S.Note(3000, HEAR_PULSE2, P("C-3"), 6, EFF_ARPEGGIO, 0x47);
		S.Halt(4800, HEAR_PULSE2);
		S.Finish();
	}
	else
		return false;
	return true;
}

}	// namespace

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

std::string Trimmed(const std::string &Text)
{
	size_t First = 0, Last = Text.size();
	while (First < Last && std::isspace(static_cast<unsigned char>(Text[First])))
		++First;
	while (Last > First && std::isspace(static_cast<unsigned char>(Text[Last - 1])))
		--Last;
	return Lowercase(Text.substr(First, Last - First));
}

std::vector<std::string> SplitList(const char *List)
{
	std::vector<std::string> Out;
	std::string Item;
	for (const char *p = List; ; ++p) {
		if (*p == ',' || *p == 0) {
			const std::string Word = Trimmed(Item);
			if (!Word.empty())
				Out.push_back(Word);
			Item.clear();
			if (*p == 0)
				break;
		}
		else
			Item += *p;
	}
	return Out;
}

// The chip an effect's entry is for: the FDS's and the 5B's H, I and J are theirs, the others ""
std::string EntryChipOf(const EffectInfo &E)
{
	const std::string Name = E.Name;
	if (Name.compare(0, 3, "FDS") == 0)
		return "FDS";
	if (Name.compare(0, 2, "5B") == 0)
		return "5B";
	return std::string();
}

int EffectIdOf(const EffectInfo &E)
{
	const char Letter = E.Form[0];
	const std::string Chip = EntryChipOf(E);
	if (Chip == "FDS")
		return Letter == 'H' ? EFF_FDS_MOD_DEPTH : Letter == 'I' ? EFF_FDS_MOD_SPEED_HI : EFF_FDS_MOD_SPEED_LO;
	if (Chip == "5B")
		return Letter == 'H' ? EFF_SUNSOFT_ENV_TYPE : Letter == 'I' ? EFF_SUNSOFT_ENV_HI : EFF_SUNSOFT_ENV_LO;
	int Effect = EFF_NONE, Param = 0;
	ParseEffect(std::string(1, Letter) + "00", Effect, Param);
	return Effect;
}

std::string EffectEntryId(const EffectInfo &E)
{
	const std::string Chip = EntryChipOf(E);
	return "effect-" + Lowercase(E.Form) + (Chip.empty() ? std::string() : "-" + Lowercase(Chip));
}

std::string Capitalized(std::string Text)
{
	if (!Text.empty() && std::islower(static_cast<unsigned char>(Text[0])))
		Text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Text[0])));
	return Text;
}

// Where an effect is heard: on the 5B's channels for its own, else the first pulse
int EffectVoice(int Effect)
{
	return Effect == EFF_SUNSOFT_ENV_TYPE || Effect == EFF_SUNSOFT_ENV_HI || Effect == EFF_SUNSOFT_ENV_LO ? HEAR_5B : HEAR_PULSE1;
}

// The note an effect is shown off on (the slides down and the sweep down start higher)
int EffectPitch(int Effect)
{
	if (Effect == EFF_SUNSOFT_ENV_TYPE)
		return P("C-4");	// the 5B sounds an octave under the note written
	return Effect == EFF_PORTA_DOWN || Effect == EFF_SWEEPDOWN || Effect == EFF_SLIDE_DOWN ? P("C-4") : P("C-3");
}

// The effect plain and then with its value, on a voice at a pitch and volume: false with why when that can't be heard
bool WriteEffectExample(Score &S, int Voice, int Effect, int Param, int Pitch, int Volume, const ChannelInfo &Channel,
	std::string &Why)
{
	const int x = (Param >> 4) & 15, y = Param & 15;
	const int Vol = Volume >= 0 ? Volume : 12;
	const std::string Written = EffectText(Effect, Param);
	if (IsSongEffect(Effect) || Effect == EFF_VOLUME) {
		Why = Effect == EFF_VOLUME ? Written + " is an old way to set the volume: the note's volume column does the same, and you "
			"hear it as the note gets louder or softer." : Written + " steers the whole song rather than a note: play the song "
			"across it to hear it.";
		return false;
	}
	const bool Off = IsStickyEffect(Effect) && Param == StickyOff(Effect);
	if (Off && Effect != EFF_PAN) {
		Why = Written + " turns the effect off, so the note plays plain from here on.";
		return false;
	}
	if (Effect == EFF_PAN && Param == 0x80) {
		Why = "880 is the middle, where every channel starts: it puts a channel panned elsewhere back in the middle.";
		return false;
	}
	if (Effect == EFF_DAC) {
		Why = Written + " sets the DPCM channel's level: you hear it as the triangle and the noise getting quieter (and a pop "
			"where the level jumps), so play the song across it.";
		return false;
	}
	if ((Effect == EFF_SWEEPUP || Effect == EFF_SWEEPDOWN) && !Channel.Chip.empty() &&
		!(Channel.Kind == KIND_PULSE && Channel.Chip == "2A03")) {
		Why = "Only the 2A03's pulses sweep: " + Written + " does nothing on this channel.";
		return false;
	}
	switch (Effect) {
	case EFF_PORTAMENTO: {
		const int Steps[] = { 0, 4, 7, 4, 0 };
		for (int Way = 0; Way < 2; ++Way)
			for (int i = 0; i < 5; ++i) {
				const int At = Way * 1800 + i * 330;
				S.Note(At, Voice, Pitch + Steps[i], Vol, Way == 1 && i == 0 ? Effect : EFF_NONE, Param);
				if (i == 4)
					S.Halt(At + 400, Voice);
			}
		S.Set(3600, Voice, -1, EFF_PORTAMENTO, 0);
		return true;
	}
	case EFF_DELAY:
	case EFF_NOTE_CUT:
		for (int Way = 0; Way < 2; ++Way)
			for (int i = 0; i < 4; ++i) {
				const int At = Way * 2200 + i * 450;
				const bool With = Way == 1 && (Effect == EFF_NOTE_CUT || i % 2 == 1);
				S.Note(At, Voice, Pitch + (i == 2 ? 7 : 0), Vol, With ? Effect : EFF_NONE, Param);
				S.Halt(At + 400, Voice);
				if (Effect == EFF_DELAY && Channel.Kind != KIND_NOISE)
					S.Hit(At, HEAR_NOISE, 0xC, 7, 1, 40);
			}
		return true;
	case EFF_VOLUME_SLIDE:
		S.Note(0, Voice, Pitch, Vol);
		S.Halt(900, Voice);
		S.Note(1100, Voice, Pitch, x > 0 && y == 0 ? 2 : (Volume >= 0 ? Volume : 15), Effect, Param);
		S.Halt(3300, Voice);
		return true;
	case EFF_SUNSOFT_ENV_TYPE:
		S.Note(0, Voice, Pitch, Vol);
		S.Halt(800, Voice);
		S.Set(950, Voice, -1, EFF_DUTY_CYCLE, 4);
		S.Note(1050, Voice, x == 0 ? Pitch : Pitch - 12, Vol, Effect, Param);
		S.Halt(2400, Voice);
		return true;
	case EFF_PITCH:
		if (Voice == HEAR_PULSE1) {
			// A pitch a step off is heard against the same note in tune: a chorus
			S.Note(0, HEAR_PULSE1, Pitch, 9);
			S.Note(0, HEAR_PULSE2, Pitch, 9);
			S.Halt(1200, HEAR_PULSE1);
			S.Halt(1200, HEAR_PULSE2);
			S.Note(1400, HEAR_PULSE1, Pitch, 9);
			S.Note(1400, HEAR_PULSE2, Pitch, 9, Effect, Param);
			S.Halt(3200, HEAR_PULSE1);
			S.Halt(3200, HEAR_PULSE2);
			return true;
		}
		break;
	case EFF_DUTY_CYCLE:
		if (Voice == HEAR_PULSE1) {
			S.Note(0, Voice, Pitch, Vol, EFF_DUTY_CYCLE, 0);
			S.Halt(800, Voice);
			S.Note(1000, Voice, Pitch, Vol, Effect, Param);
			S.Halt(1800, Voice);
			return true;
		}
		break;
	default:
		break;
	}
	const int Plain = Effect == EFF_VIBRATO || Effect == EFF_TREMOLO ? 1000 : 700;
	S.Note(0, Voice, Pitch, Vol);
	S.Halt(Plain, Voice);
	S.Note(Plain + 200, Voice, Pitch, Vol, Effect, Param);
	S.Halt(Plain + 200 + (Effect == EFF_VIBRATO || Effect == EFF_TREMOLO ? 1700 : 1300), Voice);
	return true;
}

const char *const EFFECT_EXAMPLE = "effect-";

struct Built {
	std::vector<GlossaryEntry> Entries;
	std::map<std::string, size_t> Keys;		// squeezed ids, terms and names (or the plain text of "#", "===", "---")
	std::map<int, size_t> Effects;			// effect_id, the FDS's and the 5B's apart (by Chip below), to its entry
	std::map<int, size_t> FdsEffects, S5BEffects;
};

std::string KeyOf(const std::string &Name)
{
	const std::string Key = Squeezed(Name);
	return Key.empty() ? "=" + Trimmed(Name) : Key;
}

// A part's line from SongShapes.h's PartLine inside a sentence: its first letter in lower case, unless the line starts
// with the part's own name ("A again, changed a little..." for A')
std::string InSentence(const std::string &Line, const std::string &Part)
{
	std::string Name = Part;
	while (!Name.empty() && (Name[Name.size() - 1] == '\'' || std::isdigit(static_cast<unsigned char>(Name[Name.size() - 1]))))
		Name.erase(Name.size() - 1);
	if (Line.empty() || Line.compare(0, Name.size() + 1, Name + " ") == 0)
		return Line;
	return std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(Line[0])))) + Line.substr(1);
}

// The song shapes Module > Song shape lays out: "a lively stage, a calm night, ... or a jingle"
std::string ShapeTitles()
{
	const std::vector<SongShape> &Shapes = SongShapes();
	std::string Out;
	for (size_t i = 0; i < Shapes.size(); ++i) {
		const std::string Title = Lowercase(Shapes[i].Title);
		const bool Vowel = !Title.empty() && std::string("aeiou").find(Title[0]) != std::string::npos;
		Out += (i == 0 ? "" : i + 1 == Shapes.size() ? " or " : ", ") + std::string(Vowel ? "an " : "a ") + Title;
	}
	return Out;
}

// A song's form and its parts, in the words Module > Song shape and the sections' hovers use (PartLine), so the
// glossary, the sections and the tools say the same of each part
void AddSongParts(std::vector<GlossaryEntry> &All)
{
	struct PartEntry {
		const char *Id, *Term, *Names;
		const char *Lead, *Part;		// "The intro" + what PartLine says of an "Intro"
		const char *Lead2, *Part2;		// a second part's line (A'), or null
		const char *Then, *SeeAlso;
	};
	static const PartEntry PARTS[] = {
		{ "intro", "Intro", "introduction, opening", "The intro", "Intro", 0, 0,
			"It's a few bars before the tune comes in, of the chords, the beat or a hint of the melody, often four in game "
			"music.", "song-form, a-part, loop" },
		{ "a-part", "A part (main theme)", "a, a section, main theme, a2, a', a prime", "The A part is", "A",
			"A' (A prime) or A2 is", "A'",
			"A comes back more than any other part, so it's where the song feels at home.", "song-form, b-part, melody, hook" },
		{ "b-part", "B part (contrast)", "b, b section, contrast", "The B part is", "B", 0, 0,
			"It can have a tune of its own, higher or lower; a song without one can grow dull.", "song-form, a-part, bridge" },
		{ "bridge", "Bridge", "bridges, middle eight", "A bridge is", "Bridge", 0, 0,
			"It often comes just before the last A, with other chords or a change of feel, and builds a moment of suspense.",
			"song-form, b-part, tension-release" },
		{ "climax", "Climax", "peak, high point, finale", "The climax is", "Climax", 0, 0,
			"It usually comes late in the song: everything before it builds toward it, and the music often calms down after.",
			"song-form, dynamics, layers, tension-release" },
		{ "break", "Break", "breakdown, breather", "A break is", "Break", 0, 0,
			"In game music a short break often comes just before the loop starts again, so the return sounds fresh.",
			"song-form, layers, loop" },
		{ "outro", "Outro (ending)", "ending, coda, end", "The outro is", "Outro", 0, 0,
			"Looping game music usually has none, and a jingle that plays once is almost all ending.",
			"song-form, home-note, loop" },
		{ "verse", "Verse", "verses", "The verse is", "Verse", 0, 0,
			"In a song with words each verse has new words while the chorus keeps its own; game music more often calls its "
			"parts A and B.", "chorus, song-form, a-part" },
		{ "chorus", "Chorus", "choruses, refrain", "The chorus is", "Chorus", 0, 0,
			"In a song with words it's the part everyone sings along to; in game music the A part often does its job. The "
			"chorus effect, two copies of a sound a little out of tune, is something else: detune.", "verse, hook, song-form, detune" },
	};
	for (size_t i = 0; i < sizeof(PARTS) / sizeof(PARTS[0]); ++i) {
		const PartEntry &P = PARTS[i];
		GlossaryEntry E;
		E.Id = P.Id;
		E.Term = P.Term;
		E.Names = SplitList(P.Names);
		E.Category = GLOSSARY_MUSIC;
		E.Text = std::string(P.Lead) + " " + InSentence(PartLine(P.Part), P.Part);
		if (P.Lead2 != 0)
			E.Text += std::string(" ") + P.Lead2 + " " + InSentence(PartLine(P.Part2), P.Part2);
		E.Text += std::string(" ") + P.Then;
		E.SeeAlso = SplitList(P.SeeAlso);
		All.push_back(E);
	}
	GlossaryEntry Form;
	Form.Id = "song-form";
	Form.Term = "Song form (structure)";
	Form.Names = SplitList("form, structure, song structure, aaba, song parts, song shape, song shapes");
	Form.Category = GLOSSARY_MUSIC;
	Form.Text = "A song's form is the order of its parts, often written with letters: A A B A is the tune twice, something "
		"different, then the tune again. Game music often has an intro that plays once, then A, A again, B and a break, "
		"looping back to the A. Named sections in FamiTracker show a song's form, and Module > Song shape lays one out in one "
		"click for " + ShapeTitles() + ".";
	Form.SeeAlso = SplitList("intro, a-part, b-part, bridge, climax, break, sections, loop");
	All.push_back(Form);
}

Built Build()
{
	Built B;
	std::vector<GlossaryEntry> &All = B.Entries;
	for (size_t i = 0; i < sizeof(ENTRIES) / sizeof(ENTRIES[0]); ++i) {
		const RawEntry &R = ENTRIES[i];
		GlossaryEntry E;
		E.Id = R.Id;
		E.Term = R.Term;
		E.Names = SplitList(R.Names);
		E.Category = R.Category;
		E.Text = R.Text;
		E.SeeAlso = SplitList(R.SeeAlso);
		E.Hear = R.Hear;
		E.HearHow = R.HearHow;
		All.push_back(E);
	}
	AddSongParts(All);
	// Each category in alphabetical order of its terms (the effects keep the table's order of letters)
	std::stable_sort(All.begin(), All.end(), [](const GlossaryEntry &a, const GlossaryEntry &b) {
		if (a.Category != b.Category)
			return a.Category < b.Category;
		return Lowercase(a.Term) < Lowercase(b.Term);
	});
	std::set<std::string> Taken;
	for (size_t i = 0; i < All.size(); ++i) {
		Taken.insert(KeyOf(All[i].Id));
		Taken.insert(KeyOf(All[i].Term));
		for (size_t n = 0; n < All[i].Names.size(); ++n)
			Taken.insert(KeyOf(All[i].Names[n]));
	}
	// What chords do in a key, in the words of the piano roll's chord strip (ChordRoles.h)
	{
		struct RoleEntry {
			chord_role_t Role;
			const char *Id, *Term, *Names, *Examples, *Hear, *SeeAlso;
		};
		static const RoleEntry ROLES[] = {
			{ CHORD_ROLE_HOME, "chord-home", "Home chord (tonic)", "home chord, tonic chord, I chord, one chord",
				"In C major that's C, then Am and Em; in A minor Am, then C.", "home",
				"chord-roles, home-note, chord-tension, key" },
			{ CHORD_ROLE_AWAY, "chord-away", "Away chord (subdominant)", "away chord, away, subdominant, moving off, IV chord",
				"In C major they're F and Dm; in A minor Dm, F and Bdim.", "roles",
				"chord-roles, chord-home, chord-tension, chord-progression" },
			{ CHORD_ROLE_TENSION, "chord-tension", "Tension chord (dominant)", "tension chord, dominant, dominant chord, "
				"V chord, five chord", "In C major they're G, G7 and Bdim; in A minor E, E7 and G.", "tension",
				"chord-roles, tension-release, seventh, chord-home" },
			{ CHORD_ROLE_COLOR, "chord-color", "Color chord (borrowed chord)", "color chord, borrowed chord, borrowed, "
				"borrowed from minor, borrowed from major, secondary dominant",
				"In C major Fm and Bb are borrowed from C minor, and D7 before G is G's own tension.", "borrowed",
				"chord-roles, major-minor, chord-progression, key" },
		};
		for (size_t i = 0; i < sizeof(ROLES) / sizeof(ROLES[0]); ++i) {
			const RoleEntry &R = ROLES[i];
			GlossaryEntry E;
			E.Id = R.Id;
			E.Term = R.Term;
			E.Names = SplitList(R.Names);
			E.Category = GLOSSARY_MUSIC;
			E.Text = std::string(ChordRoleExplained(R.Role)) + " " + R.Examples + " The piano roll's chord strip marks them " +
				ChordRoleName(R.Role) + ": " + ChordRoleMeaning(R.Role) + ".";
			E.SeeAlso = SplitList(R.SeeAlso);
			E.Hear = R.Hear;
			All.push_back(E);
		}
		GlossaryEntry Roles;
		Roles.Id = "chord-roles";
		Roles.Term = "Chord roles (home, away, tension, color)";
		Roles.Names = SplitList("chord role, roles, chord function, harmonic function, function of a chord");
		Roles.Category = GLOSSARY_MUSIC;
		Roles.Text = std::string("In a key, each chord has a job: home (") + ChordRoleMeaning(CHORD_ROLE_HOME) + "), away (" +
			ChordRoleMeaning(CHORD_ROLE_AWAY) + "), tension (" + ChordRoleMeaning(CHORD_ROLE_TENSION) + ") or color (" +
			ChordRoleMeaning(CHORD_ROLE_COLOR) + "). The piano roll's chord strip colors each chord by its job and names it "
			"when the mouse rests on it. Many progressions make a little trip: home, away, tension and home again.";
		Roles.SeeAlso = SplitList("chord-home, chord-away, chord-tension, chord-color, chord-progression, key");
		Roles.Hear = "roles";
		All.push_back(Roles);
		std::stable_sort(All.begin(), All.end(), [](const GlossaryEntry &a, const GlossaryEntry &b) {
			if (a.Category != b.Category)
				return a.Category < b.Category;
			return Lowercase(a.Term) < Lowercase(b.Term);
		});
		for (size_t i = 0; i < All.size(); ++i)
			if (All[i].Category == GLOSSARY_MUSIC && All[i].Id.compare(0, 6, "chord-") == 0) {
				Taken.insert(KeyOf(All[i].Id));
				Taken.insert(KeyOf(All[i].Term));
				for (size_t n = 0; n < All[i].Names.size(); ++n)
					Taken.insert(KeyOf(All[i].Names[n]));
			}
	}
	// The effects, from the effects tool's own table
	std::vector<EffectInfo> Effects = EffectReference();
	const std::vector<EffectInfo> &Song = SongEffectReference();
	Effects.insert(Effects.end(), Song.begin(), Song.end());
	for (size_t i = 0; i < Effects.size(); ++i) {
		const EffectInfo &I = Effects[i];
		const std::string Chip = EntryChipOf(I);
		GlossaryEntry E;
		E.Id = EffectEntryId(I);
		E.Term = Capitalized(I.Name) + " (" + I.Form + ")";
		E.Category = GLOSSARY_EFFECTS;
		E.Text = I.Plain;
		const std::string Form = Lowercase(I.Form);
		std::vector<std::string> Names;
		if (Chip.empty()) {
			Names.push_back(Form);
			const std::string Other = Form.substr(0, 1) + (Form.substr(1) == "xx" ? "xy" : "xx");
			Names.push_back(Other);
			Names.push_back(Form.substr(0, 1) + " effect");
		}
		else {
			Names.push_back(Lowercase(Chip) + " " + Form);
			Names.push_back(Form + " " + Lowercase(Chip));
		}
		// (its name as a word to look for: "fds modulation speed high")
		std::string Called = Lowercase(I.Name);
		for (size_t c = Called.find(", "); c != std::string::npos; c = Called.find(", "))
			Called.erase(c, 1);
		Names.push_back(Called);
		Names.push_back(Called + " effect");
		for (size_t n = 0; n < Names.size(); ++n)
			if (Taken.insert(KeyOf(Names[n])).second)
				E.Names.push_back(Names[n]);
		const int Id = EffectIdOf(I);
		// See also: what it does in words, and its cousins
		static const struct { int Effect; const char *SeeAlso; } SEE[] = {
			{ EFF_ARPEGGIO, "arpeggio, chord, arpeggio-sequence" }, { EFF_PORTA_UP, "slide, effect-2xx, effect-qxy" },
			{ EFF_PORTA_DOWN, "slide, effect-1xx, effect-rxy" }, { EFF_PORTAMENTO, "slide, staccato-legato" },
			{ EFF_VIBRATO, "vibrato, pitch-sequence, effect-7xy" }, { EFF_TREMOLO, "effect-4xy, effect-axy, dynamics" },
			{ EFF_PAN, "panning, mixer, echo" }, { EFF_VOLUME_SLIDE, "dynamics, volume-column, effect-7xy" },
			{ EFF_DELAY, "swing, rhythm, tick" }, { EFF_SWEEPUP, "effect-ixy, slide, pulse" }, { EFF_SWEEPDOWN, "effect-hxy, slide, kick" },
			{ EFF_PITCH, "detune, echo" }, { EFF_SLIDE_UP, "slide, effect-rxy, effect-3xx" },
			{ EFF_SLIDE_DOWN, "slide, effect-qxy, effect-2xx" }, { EFF_NOTE_CUT, "staccato-legato, release-halt, tick" },
			{ EFF_DUTY_CYCLE, "pulse-width, noise, duty-sequence" }, { EFF_DPCM_PITCH, "dpcm, effect-xxx, effect-yxx" },
			{ EFF_RETRIGGER, "dpcm, effect-wxx, fill" }, { EFF_SAMPLE_OFFSET, "dpcm, effect-wxx" },
			{ EFF_DAC, "dpcm, mix, triangle" }, { EFF_FDS_MOD_DEPTH, "fds, effect-ixx-fds, effect-jxx-fds" },
			{ EFF_FDS_MOD_SPEED_HI, "fds, effect-hxx-fds, effect-jxx-fds" }, { EFF_FDS_MOD_SPEED_LO, "fds, effect-hxx-fds, effect-ixx-fds" },
			{ EFF_SUNSOFT_ENV_TYPE, "5b, effect-ixx-5b, effect-jxx-5b, mode-sequence-5b" },
			{ EFF_SUNSOFT_ENV_HI, "5b, effect-hxy-5b, effect-jxx-5b" }, { EFF_SUNSOFT_ENV_LO, "5b, effect-hxy-5b, effect-ixx-5b" },
			{ EFF_SPEED, "speed, tempo-setting, tempo, swing" }, { EFF_JUMP, "loop, frame, effect-cxx" },
			{ EFF_SKIP, "frame, pattern-length, effect-bxx" }, { EFF_HALT, "loop, outro, effect-bxx" },
			{ EFF_VOLUME, "volume-column, dynamics" },
		};
		for (size_t s = 0; s < sizeof(SEE) / sizeof(SEE[0]); ++s)
			if (SEE[s].Effect == Id)
				E.SeeAlso = SplitList(SEE[s].SeeAlso);
		if (I.Example >= 0)
			E.Hear = EFFECT_EXAMPLE + Form + (Chip.empty() ? std::string() : "-" + Lowercase(Chip));
		else if (Id == EFF_DPCM_PITCH || Id == EFF_RETRIGGER || Id == EFF_SAMPLE_OFFSET || Id == EFF_DAC)
			E.HearHow = "The DPCM channel needs a sample to play: put the effect on a note of the DPCM channel, then put the "
				"cursor on it and use Help > Hear the effect at the cursor.";
		else if (Chip == "FDS")
			E.HearHow = "Put it on an FDS note whose instrument has a modulator (the FDS's sounds in Instrument > Sound browser "
				"have one), then put the cursor on it and use Help > Hear the effect at the cursor.";
		else if (Chip == "5B")
			E.HearHow = "Put it on a 5B note with H set to x 0 (like H08), then put the cursor on it and use Help > Hear the "
				"effect at the cursor.";
		else if (Id == EFF_VOLUME)
			E.HearHow = "Give notes a volume in the volume column instead and play them: louder and softer are what it does.";
		else
			E.HearHow = "Play the song across it: it steers the whole song rather than a note.";
		const size_t Index = All.size();
		if (Chip == "FDS")
			B.FdsEffects[Id] = Index;
		else if (Chip == "5B")
			B.S5BEffects[Id] = Index;
		else
			B.Effects[Id] = Index;
		All.push_back(E);
	}
	for (size_t i = 0; i < All.size(); ++i) {
		B.Keys.insert(std::make_pair(KeyOf(All[i].Id), i));
		B.Keys.insert(std::make_pair(KeyOf(All[i].Term), i));
		for (size_t n = 0; n < All[i].Names.size(); ++n)
			B.Keys.insert(std::make_pair(KeyOf(All[i].Names[n]), i));
	}
	return B;
}

const Built &TheGlossary()
{
	static const Built B = Build();
	return B;
}

// A word in the text (a whole word, or the start of one: "chord" finds "chords")
bool HasWord(const std::string &Text, const std::string &Word)
{
	size_t At = 0;
	while ((At = Text.find(Word, At)) != std::string::npos) {
		if (At == 0 || !std::isalnum(static_cast<unsigned char>(Text[At - 1])))
			return true;
		++At;
	}
	return false;
}

std::vector<std::string> Words(const std::string &Text)
{
	std::vector<std::string> Out;
	std::string Word;
	const std::string Lower = Lowercase(Text);
	for (size_t i = 0; i <= Lower.size(); ++i) {
		const char c = i < Lower.size() ? Lower[i] : ' ';
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '#' || c == '/' || c == '%' || c == '.' || c == '\'')
			Word += c;
		else {
			if (!Word.empty())
				Out.push_back(Word);
			Word.clear();
		}
	}
	return Out;
}

}	// namespace

const char *GlossaryCategoryName(int Category)
{
	switch (Category) {
	case GLOSSARY_MUSIC: return "Making music";
	case GLOSSARY_SOUNDS: return "Sounds";
	case GLOSSARY_TRACKER: return "The tracker";
	case GLOSSARY_EFFECTS: return "Effects";
	default: return "";
	}
}

const std::vector<GlossaryEntry> &GlossaryEntries()
{
	return TheGlossary().Entries;
}

const GlossaryEntry *FindGlossaryEntry(const std::string &Name)
{
	const Built &B = TheGlossary();
	const std::string Word = Trimmed(Name);
	if (Word.empty())
		return 0;
	std::map<std::string, size_t>::const_iterator Found = B.Keys.find(KeyOf(Word));
	if (Found != B.Keys.end())
		return &B.Entries[Found->second];
	const std::string Key = Squeezed(Word);
	// An effect as written ("447", "Q31") or its letter alone ("4", "q"): its entry (H and I the 2A03's sweep)
	const bool Written = Key.size() == 3 && std::isxdigit(static_cast<unsigned char>(Key[1])) &&
		std::isxdigit(static_cast<unsigned char>(Key[2])) && (std::isdigit(static_cast<unsigned char>(Key[1])) ||
		std::isdigit(static_cast<unsigned char>(Key[2])) || Name != Lowercase(Name));
	if (Written || Key.size() == 1) {
		int Effect = EFF_NONE, Param = 0;
		if (ParseEffect(std::string(1, Key[0]) + "00", Effect, Param)) {
			std::map<int, size_t>::const_iterator E = B.Effects.find(Effect);
			if (E != B.Effects.end())
				return &B.Entries[E->second];
		}
		if (std::tolower(static_cast<unsigned char>(Key[0])) == 'j' && !B.FdsEffects.empty())
			return &B.Entries[B.FdsEffects.begin()->second];
	}
	// One more or one less "s"
	if (Key.size() > 3) {
		const std::string Other = Key[Key.size() - 1] == 's' ? Key.substr(0, Key.size() - 1) : Key + "s";
		Found = B.Keys.find(Other);
		if (Found != B.Keys.end())
			return &B.Entries[Found->second];
	}
	// The one term that starts with the word ("portamento" for Portamento (3xx))
	if (Key.size() >= 4) {
		const GlossaryEntry *pOnly = 0;
		int Count = 0;
		for (size_t i = 0; i < B.Entries.size(); ++i)
			if (Squeezed(B.Entries[i].Term).compare(0, Key.size(), Key) == 0) {
				pOnly = &B.Entries[i];
				++Count;
			}
		if (Count == 1)
			return pOnly;
	}
	return 0;
}

std::vector<const GlossaryEntry *> SearchGlossary(const std::string &Text, int Category)
{
	const Built &B = TheGlossary();
	std::vector<const GlossaryEntry *> Out;
	const std::vector<std::string> Asked = Words(Text);
	if (Asked.empty()) {
		for (size_t i = 0; i < B.Entries.size(); ++i)
			if (Category < 0 || B.Entries[i].Category == Category)
				Out.push_back(&B.Entries[i]);
		return Out;
	}
	const std::string Key = Squeezed(Text);
	const GlossaryEntry *pExact = FindGlossaryEntry(Text);
	std::vector<std::pair<int, size_t> > Scored;
	for (size_t i = 0; i < B.Entries.size(); ++i) {
		const GlossaryEntry &E = B.Entries[i];
		if (Category >= 0 && E.Category != Category)
			continue;
		std::string Titles = Lowercase(E.Term);
		bool Starts = !Key.empty() && Squeezed(E.Term).compare(0, Key.size(), Key) == 0;
		for (size_t n = 0; n < E.Names.size(); ++n) {
			Titles += " | " + E.Names[n];
			Starts = Starts || (!Key.empty() && Squeezed(E.Names[n]).compare(0, Key.size(), Key) == 0);
		}
		const std::string Body = Lowercase(E.Text);
		bool InTitles = true, InBody = true;
		for (size_t w = 0; w < Asked.size(); ++w) {
			InTitles = InTitles && HasWord(Titles, Asked[w]);
			InBody = InBody && (HasWord(Titles, Asked[w]) || HasWord(Body, Asked[w]));
		}
		int Score = 0;
		if (&E == pExact)
			Score = 100;
		else if (Starts)
			Score = 80;
		else if (InTitles)
			Score = 60;
		else if (InBody)
			Score = 30;
		if (Score > 0)
			Scored.push_back(std::make_pair(-Score, i));
	}
	std::stable_sort(Scored.begin(), Scored.end());
	for (size_t i = 0; i < Scored.size(); ++i)
		Out.push_back(&B.Entries[Scored[i].second]);
	return Out;
}

std::string GlossaryFirstSentence(const GlossaryEntry &Entry)
{
	const std::string &Text = Entry.Text;
	for (size_t i = 0; i + 1 < Text.size(); ++i)
		if ((Text[i] == '.' || Text[i] == '!' || Text[i] == '?') && Text[i + 1] == ' ')
			return Text.substr(0, i + 1);
	return Text;
}

std::string GlossaryText(const GlossaryEntry &Entry)
{
	std::string Out = Entry.Term;
	if (!Entry.Names.empty()) {
		Out += " (also: ";
		for (size_t i = 0; i < Entry.Names.size(); ++i)
			Out += (i == 0 ? "" : ", ") + Entry.Names[i];
		Out += ")";
	}
	Out += std::string(". ") + GlossaryCategoryName(Entry.Category) + ".\n" + Entry.Text;
	HearExample Example;
	if (!Entry.Hear.empty() && GlossaryExample(Entry.Hear, Example))
		Out += "\nHear it (FamiTracker's Help > Glossary plays it): " + Example.Says + (Example.Chip.empty() ? std::string() :
			" It needs the " + Example.Chip + ".");
	else if (!Entry.HearHow.empty())
		Out += "\nTo hear it: " + Entry.HearHow;
	if (!Entry.SeeAlso.empty()) {
		std::string See;
		for (size_t i = 0; i < Entry.SeeAlso.size(); ++i) {
			const GlossaryEntry *pOther = FindGlossaryEntry(Entry.SeeAlso[i]);
			if (pOther != 0)
				See += (See.empty() ? "" : ", ") + pOther->Term;
		}
		if (!See.empty())
			Out += "\nSee also: " + See + ".";
	}
	return Out;
}

std::string EffectGlossaryId(int Effect, const ChannelInfo &Channel)
{
	const Built &B = TheGlossary();
	if (Channel.Chip == "FDS" && (Effect == EFF_SWEEPUP || Effect == EFF_SWEEPDOWN))
		Effect = Effect == EFF_SWEEPUP ? EFF_FDS_MOD_DEPTH : EFF_FDS_MOD_SPEED_HI;
	else if (Channel.Chip == "5B" && (Effect == EFF_SWEEPUP || Effect == EFF_SWEEPDOWN || Effect == EFF_FDS_MOD_SPEED_LO))
		Effect = Effect == EFF_SWEEPUP ? EFF_SUNSOFT_ENV_TYPE : Effect == EFF_SWEEPDOWN ? EFF_SUNSOFT_ENV_HI : EFF_SUNSOFT_ENV_LO;
	// (the FDS's and the 5B's effect_ids are their own)
	const std::map<int, size_t> *const MAPS[] = { &B.Effects, &B.FdsEffects, &B.S5BEffects };
	for (size_t m = 0; m < sizeof(MAPS) / sizeof(MAPS[0]); ++m) {
		const std::map<int, size_t>::const_iterator Found = MAPS[m]->find(Effect);
		if (Found != MAPS[m]->end())
			return B.Entries[Found->second].Id;
	}
	return std::string();
}

const std::vector<GlossaryLabel> &GlossaryLabels()
{
	static const std::vector<GlossaryLabel> All = {
		// The song settings (and the BPM layout's words: its tempo box is BPM, or Speed for a song at speed 1)
		{ "song settings", "Speed", "speed", 0 }, { "song settings", "Tempo", "tempo-setting", 0 }, { "song settings", "BPM", "tempo", 0 },
		{ "song settings", "Rows", "pattern-length", 0 }, { "song settings", "Frames", "frame", 0 }, { "song settings", "Step", "edit-step", 0 },
		{ "song settings", "Row highlight, 1st", "highlight", 0 }, { "song settings", "2nd", "highlight", 0 },
		{ "song settings", "Octave", "octave-box", "octave" },
		// The instrument editor's sequences, as each chip's editor names them
		{ "instrument editor", "Volume", "volume-sequence", 0 }, { "instrument editor", "Arpeggio", "arpeggio-sequence", 0 },
		{ "instrument editor", "Pitch", "pitch-sequence", 0 }, { "instrument editor", "Hi-pitch", "hi-pitch-sequence", 0 },
		{ "instrument editor", "Duty / Noise", "duty-sequence", 0 }, { "instrument editor", "Pulse Width", "duty-sequence", 0 },
		{ "instrument editor", "Wave", "wave-sequence", 0 }, { "instrument editor", "Noise / mode", "mode-sequence-5b", 0 },
		// The mixer's knobs and pan
		{ "mixer", "Soft", "soft", 0 }, { "mixer", "Space", "space", 0 }, { "mixer", "Pan", "panning", 0 },
		// The piano roll's buttons
		{ "piano roll", "Key", "key", "chord-roles" }, { "piano roll", "Chords", "chord", "chord-roles" },
		{ "piano roll", "Snap to key", "key", 0 }, { "piano roll", "Groove", "groove", 0 },
	};
	return All;
}

std::string GlossaryLabelId(const std::string &Where, const std::string &Label)
{
	const std::vector<GlossaryLabel> &All = GlossaryLabels();
	const std::string Asked = Trimmed(Label);
	for (size_t i = 0; i < All.size(); ++i)
		if (Where == All[i].Where && Asked == Lowercase(All[i].Label))
			return All[i].Id;
	return std::string();
}

std::string GlossaryLabelAlso(const std::string &Where, const std::string &Label)
{
	const std::vector<GlossaryLabel> &All = GlossaryLabels();
	const std::string Asked = Trimmed(Label);
	for (size_t i = 0; i < All.size(); ++i)
		if (Where == All[i].Where && Asked == Lowercase(All[i].Label))
			return All[i].Also != 0 ? All[i].Also : std::string();
	return std::string();
}

bool GlossaryExample(const std::string &Id, HearExample &Out)
{
	if (Id.compare(0, std::char_traits<char>::length(EFFECT_EXAMPLE), EFFECT_EXAMPLE) == 0) {
		// An effect's: plain, then with the value that shows it off
		const GlossaryEntry *pEntry = FindGlossaryEntry(Id);
		std::vector<EffectInfo> Effects = EffectReference();
		for (size_t i = 0; pEntry != 0 && i < Effects.size(); ++i) {
			const EffectInfo &I = Effects[i];
			if (EffectEntryId(I) != pEntry->Id || I.Example < 0)
				continue;
			const int Effect = EffectIdOf(I);
			const int Voice = EffectVoice(Effect);
			ChannelInfo Channel;
			Channel.Kind = Voice == HEAR_5B ? KIND_SQUARE : KIND_PULSE;
			Channel.Chip = Voice == HEAR_5B ? "5B" : "2A03";
			std::string Said = DescribeEffect(Effect, I.Example, Channel);
			Said = "A note plain, then with " + EffectText(Effect, I.Example) + " (" + Said + ").";
			Score S(Out, Id.c_str(), Said.c_str(), Voice == HEAR_5B ? "5B" : "");
			std::string Why;
			if (!WriteEffectExample(S, Voice, Effect, I.Example, EffectPitch(Effect), -1, Channel, Why))
				return false;
			S.Finish();
			return true;
		}
		return false;
	}
	return MakeExample(Id, Out);
}

std::vector<std::string> GlossaryExampleIds()
{
	std::set<std::string> Ids;
	const std::vector<GlossaryEntry> &All = GlossaryEntries();
	for (size_t i = 0; i < All.size(); ++i)
		if (!All[i].Hear.empty())
			Ids.insert(All[i].Hear);
	return std::vector<std::string>(Ids.begin(), Ids.end());
}

bool EffectHearExample(int Effect, int Param, const ChannelInfo &Channel, int Pitch, int Volume, HearExample &Out,
	std::string &Why)
{
	Why.clear();
	if (Channel.Chip == "FDS" && (Effect == EFF_SWEEPUP || Effect == EFF_SWEEPDOWN))
		Effect = Effect == EFF_SWEEPUP ? EFF_FDS_MOD_DEPTH : EFF_FDS_MOD_SPEED_HI;
	else if (Channel.Chip == "5B" && (Effect == EFF_SWEEPUP || Effect == EFF_SWEEPDOWN || Effect == EFF_FDS_MOD_SPEED_LO))
		Effect = Effect == EFF_SWEEPUP ? EFF_SUNSOFT_ENV_TYPE : Effect == EFF_SWEEPDOWN ? EFF_SUNSOFT_ENV_HI : EFF_SUNSOFT_ENV_LO;
	if (Effect <= EFF_NONE || Effect >= EFF_COUNT || EffectLetter(Effect) == 0) {
		Why = "There's no effect there.";
		return false;
	}
	if (Pitch < 0)
		Pitch = Channel.Kind == KIND_NOISE ? 0x8 : P("C-3");
	const std::string Said = "The note plain, then with " + EffectText(Effect, Param) + " (" + DescribeEffect(Effect, Param,
		Channel) + ").";
	Score S(Out, "effect", Said.c_str());
	if (!WriteEffectExample(S, HEAR_HERE, Effect, Param, Pitch, Volume, Channel, Why))
		return false;
	S.Finish();
	return true;
}

std::string HearChipHow(const std::string &Chip)
{
	if (Chip == "VRC6 Plus")
		return "This song isn't a VRC6 Plus song, so it has no soft layer to play on. Module > Module properties turns VRC6 "
			"Plus on, and Instrument > Sound browser lists its soft sounds (adding one asks before the chip comes in).";
	return "This song has no " + Chip + ", so no channel here can play it. To hear the " + Chip + ", open Instrument > Sound "
		"browser: the " + Chip + "'s sounds come after the song's own, and adding one asks before it adds the chip (as Module "
		"> Module properties does).";
}

}	// namespace ftapi

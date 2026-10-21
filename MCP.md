# Claude Code in FamiTracker

FamiTracker can let [Claude Code](https://claude.com/claude-code) work on the
song you have open: read it, write parts, change the key, the tempo or the
sounds, check it for clashes and play it for you. You ask in plain words
("make the atmosphere darker"); Claude works in bars, beats, note names and
instruments, not hex rows.

It works over MCP (the Model Context Protocol). FamiTracker itself is the
MCP server, so there is nothing else to install besides Claude Code. Claude
Code in WSL connects through `famitracker-mcp.exe`, which comes in the same
zip (see [Claude Code in WSL](#claude-code-in-wsl)).

## Connecting, once

1. Install Claude Code if you haven't.
2. In FamiTracker: **File > Connect Claude Code...**
3. Tick **Let Claude Code connect**. The status line says
   "On: Claude Code can connect at 127.0.0.1:46600".
4. Decide how Claude's changes arrive (see below). Leave **Let Claude change
   the song directly** off to start with.
5. With **Claude Code runs in: Windows** picked, press **Copy**, open
   PowerShell or Command Prompt, paste and press Enter. The command looks
   like this, with your own access code:

   ```
   claude mcp add --transport http --scope user famitracker http://127.0.0.1:46600/mcp --header "Authorization: Bearer 1f3a..."
   ```

6. Press OK in FamiTracker.

From then on, start `claude` in any folder while FamiTracker is running.
`/mcp` in Claude Code shows "famitracker" as connected. FamiTracker remembers
the setting and starts listening whenever it starts.

## Claude Code in WSL

Claude Code in WSL (Linux on Windows) can't reach FamiTracker at 127.0.0.1:
WSL has a network of its own. It connects through `famitracker-mcp.exe`
instead, which comes in the same zip as FamiTracker.exe and stays next to it.
WSL runs Windows programs on Windows, so the helper reaches FamiTracker there:
no firewall rule, no mirrored networking, nothing to redo after a reboot.

1. In FamiTracker: **File > Connect Claude Code...**, tick **Let Claude Code
   connect** and pick **Claude Code runs in: WSL**.
2. Press **Copy**, paste into your WSL terminal and press Enter. The command
   looks like this, with the folder FamiTracker is in:

   ```
   claude mcp remove --scope user famitracker >/dev/null 2>&1; claude mcp add --transport stdio --scope user famitracker -- "$(wslpath 'C:\Users\you\FamiTracker\famitracker-mcp.exe')"
   ```

   It replaces an earlier `famitracker` setup, such as the Windows command
   run in WSL.
3. Press OK in FamiTracker, then start `claude` in WSL.

Claude Code starts the helper by itself. The helper reads the port and the
access code from FamiTracker's settings whenever Claude asks something, so a
new access code or port needs no new setup. If FamiTracker moves to another
folder, run the dialog's command again. It needs WSL's Windows interop, which
is on unless `/etc/wsl.conf` turns it off (`[interop] enabled=false`).

With mirrored networking (`networkingMode=mirrored` in `.wslconfig`), WSL
shares 127.0.0.1 with Windows, and the Windows command works in WSL too.

## Asking

Some things to try:

- "What's in my song?"
- "Make the atmosphere darker." / "Make it happier." / "Make it sound like an
  underwater level."
- "Add chords and a bass line under the melody."
- "Write a counter-melody on Pulse 2 for bars 5 to 8."
- "Give the lead a softer sound." / "Put a slow vibrato on the long notes."
- "Add an echo to the melody."
- "Check the song for notes that clash."
- "Play bars 1 to 4."
- "Make a night version of this song." / "Make the night version a bit less
  slow and keep the drums."
- "Make a battle version." / "Make a level-up jingle and one for finishing a
  quest."
- "Does the song loop cleanly?" / "Let bars 1-4 be an intro and loop the rest."
- "Export the day and night versions for my game."
- "Let's make a VRC7 song." / "Give the melody a piano sound."
- "Make it faster, speed 5." / "Set the tempo to 100 BPM."
- "Make a jump sound and three coin sounds to choose from." / "Turn my
  recording pew.wav into a sound effect."
- "Start a battle theme in D minor." / "Give me a groove for a spooky cave."
- "Use these chords: | Am7 | Fmaj7 | C | G7sus4 G7 |, as broken chords, with a
  walking bass."
- "Add drums with a fill every four bars." / "Give the melody a harmony a
  third below." / "Add a counter-melody under it."
- "How full does the arrangement sound? What's missing?"
- "Make a lead that swells in and gets a vibrato on long notes."
- "Repeat bars 1-8 at bar 9, but make the repeat different."
- "Remember that I like bright leads and don't want echo on the melody."

Claude can't hear the song. It checks what it wrote (notes outside the key,
clashes, range), looks at a picture of the notes, reviews how full the
arrangement is, and plays it for you, then asks how it sounds.

Four recipes come with the connection as commands in Claude Code (type `/`
to see them): `/mcp__famitracker__compose_track` composes a track for a
scene ("a snowy village at night") step by step, `/mcp__famitracker__deepen_track`
works through what makes the open song sound plain,
`/mcp__famitracker__make_versions` makes versions of a part as clips to
choose from, and `/mcp__famitracker__study_track` explains what makes a track
work, to reuse its ideas.

## How changes arrive

- **As clips (default).** Claude's ideas are saved as clips, named
  "Claude: ...". The reply says where one goes, for example "on VRC6 Pulse 1
  at bar 5". To hear it, press the play button on its card in the clips
  panel beside the song; to use it, drag the card there in the pattern
  editor. Ctrl+Z takes it out again. Right-click a clip you don't want to
  delete it.
- **Directly.** With **Let Claude change the song directly** ticked, Claude
  writes into the song. Every change is one step of Ctrl+Z (Ctrl+Y redoes it).
  Claude can then also set the tempo, place clips and undo. Instruments it
  adds from presets stay in the instrument list even when the notes are
  undone.

Without direct changes Claude doesn't change the tempo; when an idea needs
another one, the reply says which speed and tempo to set in the toolbar.

While a dialog is open in FamiTracker, Claude reads but doesn't change
anything; close the dialog and ask again.

## What Claude can do (the tools)

| Tool | What it does |
|---|---|
| `get_song` | Title, tempo in BPM, bars, each channel and what it plays, instruments, clips, the key found from the notes, the chords that fit the melody bar by bar and the chords the parts play, and whether the soundtrack has notes |
| `read_notes` | One channel's notes: bar, beat, note, length in beats, instrument, volume, effects |
| `write_notes` | Writes a part on a channel, replacing its notes in those bars (drums by name on the noise channel: kick, snare, hihat; on the DPCM channel the sampled kick, snare, low tom and high tom). Speed changes, jumps, skips and halts (`Fxx`, `Bxx`, `Dxx`, `Cxx`) stay, so the song still loops the same way |
| `place_clip` | Places a clip at a bar, as a copy or linked (direct changes only) |
| `add_instrument` | Adds a sound from a preset for a 2A03 or VRC6 channel, or one of the VRC7's own sounds |
| `design_instrument` | Makes a new instrument from a description: attack, decay, sustain and release, the pulse width and how it moves, a vibrato that comes in after a moment, a blip, scoop, fall, flick or thump on the attack, a chord arpeggio, and the classic NES articulations (an attack that blooms, a duty click, a decay in steps, held notes struck again, an echo after the note); or a drum, darker or brighter, shorter or longer. See [Sounds in words](#sounds-in-words) |
| `set_instrument` | Gives a channel's notes another instrument or preset |
| `transpose` | Moves notes by semitones, or into another key or mode (C major to C minor, lydian, dorian...); arpeggiated chords follow |
| `set_tempo` | Sets the speed and tempo, from a BPM or as FamiTracker's own numbers (direct changes only; undoable). See [Speed and tempo](#speed-and-tempo) |
| `add_chords_and_bass` | Chords fitted to the melody, or a progression you give by name or degree, played in a style, with a bass line in a style. See [Arrangements with depth](#arrangements-with-depth) |
| `make_groove` | A backing to start a track from, for a mood (town, overworld, battle, boss, dungeon, sad, calm, chase, upbeat, shop, underwater, castle, desert): chords, bass and drums, each played as that mood usually is |
| `add_drums` | A drum part on the noise channel: a groove, small changes every other bar, a fill where each phrase ends and a crash after it |
| `add_harmony` | A second voice following the melody a third (or sixth, fifth, octave) away, on the chords' notes where the melody holds |
| `add_counter_melody` | A line of its own from the chords' thirds and sevenths, holding while the melody moves and moving while it holds, below or above it |
| `polish` | Accents, a vibrato that comes in on the melody's long notes, gaps between repeated triangle notes |
| `copy_bars` | Repeats bars elsewhere (A A B A), moved up or down if asked; the copy starts as the original played |
| `vary_part` | Changes a repeat a little: passing notes, only the notes on the beats, syncopation, an octave up or down, an ending on the tonic |
| `review_arrangement` | How full the arrangement sounds, scored out of 10, and what would help most: missing layers, thin bars, plain or looping chords, a bass on roots, drums without fills, a melody without rests, sections pasted unchanged, flat dynamics |
| `soundtrack_notes` | Reads or adds to a text file next to the song that keeps your taste and the decisions about your game's music between sessions |
| `study_nsf` | Studies tracks of an NSF file you have (game music) and keeps what makes each one work in your reference library. See [References: learning from NES soundtracks](#references-learning-from-nes-soundtracks) |
| `study_module` | Studies a tracker module you have (MOD, S3M, XM or IT) from its patterns and keeps it in your reference library too, so new NES music can borrow its style. See [Modules as references](#modules-as-references) |
| `references` | Lists, shows and searches the reference library, and the built-in guide to the classic NES styles (Konami, Capcom, Nintendo) |
| `reference_sound` | Adds one of a reference's sounds to the song as an instrument, with the original's exact envelopes |
| `import_nsf` | Opens tracks of an NSF file as a new module (the same as File > Import NSF), to look at or play them |
| `add_sample_drums` | Adds a kit of sampled drums for the DPCM channel, made for FamiTracker (a kick, a snare and two toms); `write_notes` then plays them there by name |
| `add_echo` | Copies a part onto a free channel, later and quieter (it stops at the song's end, so the loop stays where it is) |
| `add_vibrato` | Vibrato on the longer notes, starting just after each note begins |
| `change_mood` | Several changes for a mood at once: key or mode, parts up or down, new sounds, vibrato, an echo and the tempo |
| `check_song` | Notes outside the key, clashing channels, notes out of a channel's range, busy and silent bars, unused channels |
| `make_version` | Another version of the selected track as a new track: a night version, a tense one for battles, an area's variant. The original stays as it was |
| `make_jingle` | A jingle as a new track: a short tune that plays once (level up, victory, item, new quest, discovery, rest, defeat, danger), in the song's key note and sounds |
| `make_sound_effect` | A sound effect as a new track: a jump, a coin, a menu blip and 22 more kinds, or a recording of your voice ("pew", "boing", "kssh"). See [Sound effects](#sound-effects) |
| `select_track` | Switches to another track of the module, by number or name; the tools work on the selected one |
| `set_expansion` | Switches the expansion chip (none, VRC6, VRC7, FDS, MMC5, N163), as Module > Module properties does. See [Expansion chips](#expansion-chips) |
| `check_loop` | How the song loops in a game: the intro that plays once, the part that repeats and how long each lasts, and the seam: notes hanging over it, effects, instruments, volumes or tempo that differ the first time and on repeat, the chords and the melody's step across it |
| `set_loop` | Sets where the loop starts (an intro before it) and ends, with a jump (`Bxx`) (direct changes only; undoable) |
| `export_for_game` | Saves tracks as looping OGG files next to the song, with the GameMaker code that plays them (the same as File > Export for games) |
| `look_at_song` | A picture of the notes for Claude to look at: a piano roll of up to 32 bars, each channel in its own color, drums in a strip below, arpeggio chords as faint bars, where the song loops, and rows that never play hatched; the reply names the chords heard bar by bar |
| `play`, `stop` | Plays bars for you |
| `undo` | Ctrl+Z (direct changes only) |
| `save_song` | Saves the song, or a copy next to it to compare versions |

Instrument presets: soft lead, bright lead, thin lead, pluck, bell, soft
chords, warm pad, bass, plucky bass, singing lead (a vibrato that comes in by
itself), strings, brass, organ, kick, snare, hi-hat, open hi-hat, crash, tom.
Each is built for the 2A03 (also MMC5 channels) and the VRC6. VRC7 channels
play the chip's own sounds instead (see [Expansion chips](#expansion-chips)).

Notes are written the way FamiTracker shows them: C-4, C#4 (Db4 works too).
FamiTracker's octave numbers are one lower than in most music software: C-3
is middle C and A-3 is 440 Hz. Bars and beats count from 1, as the row
highlight shows them (the first highlight is a beat, the second a bar).
Claude works on the track that's selected.

## Speed and tempo

FamiTracker counts time in ticks, 60 a second (50 for PAL). The **speed** is
how many ticks a row lasts, and the **tempo** stretches that: at tempo 150
(125 for PAL) a row lasts exactly the speed's ticks. Most FamiTracker songs
keep tempo 150 and change the speed. At 4 rows a beat:

| Speed | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|
| BPM at tempo 150 | 300 | 225 | 180 | 150 | 128.6 | 112.5 | 100 | 90 |

Ask for a BPM and Claude picks the speed and tempo. Every row then lasts the
same number of ticks when that's possible within 1% of the BPM, usually by
changing the speed at tempo 150. Other BPMs need another tempo, and then rows
take turns being a tick longer: 120 BPM at speed 6 and tempo 120 alternates
rows of 7 and 8 ticks. You can't hear it in most songs, but Claude says so
and names the even choices nearby. Ask for a speed or a tempo ("speed 5",
"tempo 150") and Claude sets exactly that. When the song changes its speed
with `Fxx` effects, a BPM changes the tempo only, since those effects would
set the speed back. Night and tense versions pick their tempo the same way,
unless the song has `Fxx` effects: then they scale the tempo, and the tempo
effects along with it.

## Expansion chips

Ask for "a VRC6 song", "a VRC7 song" or "an MMC5 song" and Claude switches
the expansion chip with `set_expansion`, as **Module > Module properties**
does. FamiTracker plays one expansion chip at a time, so the old chip's
channels go and the new chip's take their place, in every track:

- When the old chip's channels have notes, Claude asks you first, since
  they're cleared and FamiTracker can't undo switching chips (Module >
  Module properties switches back, without the notes). Save a copy first if
  you might want them.
- Speed changes, loops and ends (`Fxx`, `Bxx`, `Dxx`, `Cxx`) on those
  channels move to the song's last channel, so the song still loops the way
  it did.
- Expansion chips play on NTSC machines, so a PAL song becomes NTSC.

The **VRC6** adds two pulses and a sawtooth, and the **MMC5** two more pulses
(FamiTracker has no MMC5 sample channel). The presets work on all of them:
the MMC5's pulses play the 2A03's instruments, so they also take chords,
echoes and the parts of versions and jingles like the 2A03's pulses.

The **VRC7** has six FM channels with 15 sounds built in: bell, guitar,
piano, flute, clarinet, rattling bell, trumpet, reed organ, soft bell,
xylophone, vibraphone, brass, bass guitar, synthesizer and chorus. Claude
adds them as instruments by name ("a piano for the melody", "brass chords"),
and a preset's name picks the sound most like it: "soft lead" is the flute,
"bass" the bass guitar, "warm pad" the chorus. Notes end with a halt (`---`),
which lets the sound fade the way the chip does. The FDS and N163 need
wavetable instruments, which you make in FamiTracker's instrument editor.

## Arrangements with depth

Tracks sound plain when every bar is the same: one chord held as an
arpeggio, a bass on the roots, no drums, a melody of one note length, a
section pasted again unchanged. The connection tells Claude how arrangers
avoid that, and gives it tools for each layer:

- **Chords** (`add_chords_and_bass`): by name ("| Am7 | Fmaj7 | C | G7sus4
  G7 |") or as degrees of the key ("I vi IV V", "V7/V"); "." holds a chord
  for a beat, "%" repeats a bar, and the progression repeats to fill the
  bars. Or fitted to the melody, with `color` sevenths (Imaj7, ii7, V7...) or
  rich (add9, maj7, m9, a V7sus4 that resolves) where the melody doesn't rub
  against them. Played as an `arpeggio` (the 0xy shimmer, held or struck on a
  rhythm), `stabs` (short hits: offbeats, eighths, tresillo, charleston,
  waltz...), `broken` chords (up, down, updown, alberti, rolling) or a
  `guide` line (one long note a chord on its third or seventh).
- **Bass** (`bass_style`): root_fifth, roots, half, octaves, walking (with
  approach notes into each chord), syncopated, arpeggio, driving, pedal. On
  the triangle it sits where it sounds like a bass (the triangle plays an
  octave below what's written).
- **Drums** (`add_drums`): basic, four_on_floor, half_time, shuffle, march,
  breakbeat, calm, driving, waltz; light, normal or busy; an open hi-hat into
  every other bar, fills that take turns (toms, a snare roll, kick and snare)
  and crashes.
- **A second line**: `add_counter_melody` or `add_harmony`, quieter, on a
  free channel.
- **Finishing touches**: `polish`; `make_groove` starts a whole backing for
  a mood; `copy_bars` and `vary_part` build a form whose repeats change.

`review_arrangement` listens the way an arranger would and scores the depth
out of 10, with what would help most and the tool for each. Claude runs it
before calling a track done.

## Sounds in words

`design_instrument` makes an instrument from words, starting from a kind
(lead, pad, pluck, bass, keys, bell, brass, strings, organ, chiptune; kick,
snare, hihat, open hihat, crash, tom) and changing what should differ:
attack, decay, sustain and release (words or frames, 60 a second), the
pulse width (12.5%, 25%, 50%) and whether it opens up, shimmers or thins as
it fades, a vibrato built in that comes in after a delay given in beats, an
octave blip, a scoop or a fall on the attack, or a chord played as a fast
arpeggio from every note. Drums can be darker or brighter, shorter or
longer. The instrument gets a new name, so notes that use another one don't
change.

The articulations the studied NES soundtracks are made of are there too:
an attack that blooms from about two thirds of its peak in two frames, a
duty click (each note's first frame at another pulse width), a decay in
steps, held notes struck again every so many beats, a soft echo after the
note, a quick pitch flick, and a bass that thumps in from a fourth above.
The kinds "bloom lead", "stepped lead", "click pluck", "echo pluck" and
"thump bass" start from them.

## References: learning from NES soundtracks

Give Claude NSF files you have (game music, ripped as `.nsf` or `.nsfe`;
put the `.m3u` playlist of the same name next to it, and the tracks get
their names) and ask it to study them, like "study the Mega Man 2 NSF in
my Music folder". `study_nsf` plays each track that's music (jingles and
sound effects are told apart), works out its notes and writes down how
it's made: the tempo and form, the key and the chords bar by bar, what each
channel does (its role, register, rhythm as a grid of its commonest bar,
intervals, and for the bass its figure), the drums' grooves, the tricks
between channels (an echo a row behind, harmony in thirds, doubling, notes
traded) and the techniques in the sounds (duty clicks, attacks that bloom,
echoes in the envelope, decays in steps, scoops, pitch flicks, vibrato
with its delay, triangle thumps), with the main sounds' envelopes.

Each study is kept as a reference in a library of yours,
`Documents\FamiTracker references` (a JSON file each), for every song and
every session. When you ask for music "like Bucky O'Hare", "in a Mega Man
style" or "lean on the references", Claude searches the library
(`references`) and plans from what it finds, and `reference_sound` brings
in a reference's instrument exactly. The melodies aren't kept: Claude
takes the craft (grooves, bass figures, harmonic color, how the channels
share the parts, the sounds) and writes its own tunes and progressions.
Built in, without any NSF, is a guide to the classic NES styles from
studies of Konami's (Bucky O'Hare, TMNT II), Capcom's (Mega Man 2-5, Little
Nemo) and Nintendo's (Super Mario Bros. 2 and 3) soundtracks.

You can tag references and keep notes with them ("the user loves this
bass"), and `import_nsf` (or File > Import NSF) opens tracks as a module to
look at the notes themselves.

### Modules as references

Music made in the PC trackers works too: give Claude a MOD, S3M, XM or IT
file of yours, like "use song.xm in my Music folder as a reference for the
boss theme". `study_module` reads only the module's patterns (its samples
aren't read, and nothing of it goes into your song) and keeps a study in the
same library: the tempo and form, the key and chords, each channel's role,
register, rhythm and instruments by name, the bass's figure, the drums'
grooves (the drums are told by their instruments' names, or by playing one
note over and over; an instrument with a sample a key splits into its
drums), the tricks between channels, and the techniques written into the
patterns (arpeggios, vibrato, portamento, volume slides). A module has more
channels than the NES, so Claude boils it down: the lead on a pulse or the
sawtooth, harmony, pads and chords on the other pulse or as arpeggios, the
bass on the triangle, the drums on the noise and the DPCM. The tunes stay
the module's own: Claude writes new ones that sound like they belong to it.

## Moods

Moods aren't magic; Claude combines changes that each do one thing reliably,
with `change_mood`, so they arrive together: one clip to try, or with direct
changes one step of Ctrl+Z (plus one for the tempo). Separate tools would
make separate clips, each starting from the original song.

- **Darker:** the minor or phrygian mode, 10-20% slower, parts an octave
  lower, rounder or softer sounds (warm pad, soft lead), fewer notes.
- **Happier:** major or lydian, a little faster, brighter sounds (bright
  lead, bell, pluck), arpeggiated chords.
- **Underwater:** slower, a slow and strong vibrato on the lead, an echo,
  soft round sounds, a lower register.

Ask for a version as a clip or a saved copy to compare it with the original.

## Day and night versions

Ask for a night version and Claude adds it as a new track in the same file,
named like "Town (night)", next to the day version, which stays as it was.
The two share the module's instruments, and FamiTracker shows the new track
afterwards (switch with the track box in the toolbar).

The night version is, unless you ask for something else:

- 20% slower (tempo commands in the patterns slow down too),
- quieter (every channel's volume at 75%),
- with sparser, softer drums (only the hits on the beats, at 60%),
- with a soft lead for the melody and soft chords for arpeggio chords,
- and with a slow, gentle vibrato and an echo on the melody (no second echo
  when the song already has one).

Say what you want different, in words: "keep the drums", "only 10% slower",
"no echo", "the melody an octave lower", "in dorian". Claude can also change
the night version afterwards with the other tools, since it's the selected
track.

A **tense version** (for a battle, danger, a boss or a chase) is, unless you
ask for something else:

- 15% faster,
- in minor when the song is in major (a song in minor keeps its mode),
- with a bright lead for the melody,
- with a driving bass: each bass note struck again on every eighth note,
  cut short before the next (notes that slide stay as they are),
- and with busier drums: soft hi-hats between the beats the drums leave
  empty.

Other versions (rain, underwater, a dungeon) change only what you ask for.

Adding a track can't be undone with Ctrl+Z in FamiTracker; remove a version
you don't want in **Module > Module properties**.

## Jingles

Ask for a jingle ("make a level-up jingle", "a jingle for when a quest is
done") and Claude adds it as a new track: a short tune with a lead, a
harmony line below it, a bass and drums. It's in the song's key note, near
its melody's register and with its sounds (the lead's instrument, the bass's),
and it ends with a halt (`C00`), so the export for games saves it as a sound
that plays once.

| Kind | For | Length |
|---|---|---|
| level up | a level up, a new skill, a power-up | 3.2 s |
| victory | a quest completed, a battle won, a stage cleared | 3.6 s |
| item | an item, a treasure or a key found (on a bell) | 1.7 s |
| new quest | a new quest, a message, a notification (two bell chimes) | 1.5 s |
| discovery | a secret, a new area, a solved puzzle | 3.2 s |
| rest | a rest at an inn, a save, healing (slow and soft) | 6.7 s |
| defeat | a lost battle, a game over (minor) | 6.3 s |
| danger | an alarm, a trap, a boss appearing (minor, unresolved) | 2.9 s |

Defeat and danger are in minor and the others in major, on the song's key
note (or the one you name). You can ask for another tempo, other
instruments, channels, or no drums or harmony. Since the jingle is the
selected track afterwards, Claude can change its notes like any other part.

## Sound effects

Ask for a sound effect ("a jump sound", "three coin sounds to choose from")
and Claude adds it as a new track, made the NES's way: a pulse, the triangle
or the noise channel moving its volume, pitch and duty 60 times a second. The
track plays at speed 1, a row a tick, and halts at its end (`C00`), so the
export for games saves it as a sound that plays once:
`audio_play_sound(snd_sfx_jump, 0, false)`. Each part is an instrument whose
sequences shape the sound, named after the track ("SFX Jump"), so you can
change it in the instrument editor.

| Kind | For |
|---|---|
| jump, land, fall | jumping, landing, falling into a pit |
| coin, pickup, power-up, chest | coins and gems, items, upgrades, opening a chest |
| hurt, stomp, laser, explosion | taking damage, landing on an enemy, shooting ("pew"), a blast ("kssh") |
| bounce, warp, whoosh, splash, door, footstep, break | a spring ("boing"), teleporting, dashing, water, doors, steps, breaking a pot |
| select, confirm, cancel, error, text | menus, OK, back, something that can't be done, dialogue letters |
| message, heal | chat messages and notifications, healing |

Every kind has variants (variant 2, 3, ... are a little higher or lower,
shorter or longer, some with another duty), and Claude can move a sound by
semitones, stretch it, or make it softer.

Claude can also turn **a recording of your voice** into a sound effect: give
it the path of a WAV file ("C:\Users\Me\Documents\pew.wav"). Where your
voice has a pitch, a pulse follows it, sliding between notes, moved by whole
octaves so it sits where NES sound effects do; the breathy and hissy rest
plays on the noise channel, brighter for "sss" than for a breath. Silence
before and after is left out, and a sound can last up to 4.2 seconds.
FamiTracker can do this without Claude, too: Edit > Sound effect from your
voice records the sound, plays it as the NES would and adds it with this
same tool (see `RECORDER.md`). Its takes stay in
`Documents\FamiTracker recordings`, so you can ask Claude for another
version of one ("the last voice take, an octave lower").

Keep sound effects in a module of their own ("Sound effects.ftm"), so your
music file stays clean; Claude works on whichever module is open.

## Music for your game: looping OGG files

**File > Export for games (OGG)...** saves the selected track, or all of them
(day and night versions together), as OGG files next to the song, named
after the tracks: "Town.ogg", "Town (night).ogg". Ask Claude for it too
("export the music for my game").

Each file plays the song's intro once (the bars before where it loops), then
the loop, cut so it repeats without a seam:

- FamiTracker renders the song past its second time through the loop and
  notes the exact sample where every row starts.
- If the loop starts the same way both times (no note, effect, instrument or
  volume from the intro hanging over), the file is the intro and the loop
  once. Otherwise it holds the loop twice and loops the second time, which
  starts the way every repeat does. `check_loop` tells you which, and why.
- The loop points move together by up to 2 ms to where the waves meet, so
  the jump doesn't click; the loop keeps its exact length.

GameMaker (2023.2 or newer) loops a sound between two points. The dialog and
Claude's reply give the code, for example:

```
audio_sound_loop_start(snd_town_night, 12.800000);
audio_sound_loop_end(snd_town_night, 38.400000);
audio_play_sound(snd_town_night, 0, true);
```

Add each file as a sound with that name (snd_ and the file name). The loop
points are also in the files as `LOOPSTART`, `LOOPLENGTH` and `LOOPEND`
comments (in samples), which RPG Maker and other engines read. A song that
ends with a halt (`Cxx`) plays once, like a jingle.

The song has to be saved, since the files go next to it; files of the same
names are replaced. "Normal" quality suits chiptunes; "Best" makes bigger
files. Every channel is in the files, also the ones muted in the pattern
editor; they're muted again afterwards.

## Safety

- FamiTracker only listens on 127.0.0.1, so other computers can't reach it.
  Claude Code in WSL comes through `famitracker-mcp.exe`, a Windows program
  that connects to 127.0.0.1 like any other; nothing is opened to WSL's
  network.
- Every request needs the access code from the command. Web pages are turned
  away (FamiTracker checks where a request comes from), and so is anything
  without the code.
- **New access code** in the dialog makes the old command stop working. Run
  `claude mcp remove famitracker`, then the new command. In WSL there's
  nothing to do: the helper reads the new code.
- Untick **Let Claude Code connect** to turn it off.

## When something goes wrong

- **"Port 46600 is already in use"**: another program (or a second
  FamiTracker) has it. Pick another port in the dialog, then
  `claude mcp remove famitracker` and run the new command.
- **Claude Code says the server failed or isn't connected**: is FamiTracker
  running, with the connection switched on? `/mcp` in Claude Code reconnects.
- **"the access token is missing or wrong"**: the command was made with an
  older access code. Remove and add it again with the current command.
- **"A dialog is open in FamiTracker"**: close it and ask again.
- **In WSL, "FamiTracker isn't running, or it doesn't listen on port 46600"**:
  start FamiTracker, check that **Let Claude Code connect** is ticked, then
  `/mcp` in Claude Code to reconnect.
- **In WSL, the server fails to start** ("no such file", "exec format
  error"): `famitracker-mcp.exe` isn't where the command says (did
  FamiTracker move?), or WSL's Windows interop is off. Run the dialog's
  command again.
- **The Windows command in WSL connects to nothing**: that's WSL's own
  127.0.0.1. Pick **WSL** in the dialog and run that command instead.

## Not yet

- Recording ideas from a phone page into the clips.
- Instruments for the FDS and N163.

## For developers

- `Source/Api/` is plain C++14 without MFC, built and tested on Linux:
  `Json` (reader and writer), `Http` (request framing), `Mcp` (the protocol:
  per-request metadata of 2026-07-28 and the initialize handshake of
  2025-03-26 to 2025-11-25, Host, Origin and token checks), `Music` (cells,
  note names, bars and beats, keys and modes, writing a part into a range of
  rows, and following the song through its jumps, skips and halts),
  `Presets` (also the VRC7's patches, and `DesignPreset`: a sound described
  in words as sequences), `Chords` (chords read from names and degrees,
  progressions laid out on bars, colors, and the chords heard in notes),
  `Arrange` (chord parts in styles, bass lines, drum grooves with fills, a
  harmony voice, a counter-melody), `Picture` and `Png` (the piano-roll
  picture, a palette PNG with its own small deflate), `Jingles` (the
  jingles' scores, in semitones from the key note and sixteenth notes),
  `SoundEffects` (the sound effect kinds, built a tick at a time), and
  `Tools` (the tools, against the `SongHost` interface in `Song.h`, the
  server's instructions and the recipes served as MCP prompts). Tool
  results can carry an image after their text.
- `Source/Transcribe/SoundEffect.cpp`: sound effects as layers of per-tick
  values and as FamiTracker instruments (each tick an absolute arpeggio picks
  the nearest note and the pitch sequence moves the period to the pitch
  between notes; the noise uses a fixed arpeggio), `RenderEffect` (the 2A03
  played through those sequences, for the tests) and `VoiceToEffect` (a
  recording's loudness, pitch and zero crossings, tick by tick), and
  `VoiceTake` (when a take from the microphone is over).
  ```
  cmake -S Source/Api -B build/api
  cmake --build build/api
  ctest --test-dir build/api --output-on-failure
  ```
  The tests run every tool against a song in memory and the protocol with
  the requests real clients send. The protocol was also checked against the
  official MCP TypeScript SDK clients (1.x, and 2.x in both negotiation
  modes).
- `Source/Api/GameAudio.cpp`, `Ogg.cpp` and `Music.cpp` (`TimeRows`): the
  export for games. `TimeRows` counts the ticks of every row as `CSoundGen`
  plays them; `CutForGame` finds the loop in a render from the rows' sample
  positions (`CSoundGen::GetRenderMarks`, recorded while rendering); `Ogg`
  encodes with libvorbis (`Source/libvorbis`, `Source/libogg`, BSD licensed,
  as released). The tests render with a stand-in synthesizer and decode the
  files again.
- `Source/McpSocket.cpp`: the listener thread (Winsock, no MFC). It frames
  HTTP requests and hands each to the UI thread with `SendMessageTimeout`.
- `Source/Api/Bridge.cpp` and `Source/Api/tools/famitracker-mcp.cpp`: the
  connection for Claude Code in WSL, an MCP server over stdio. Each JSON-RPC
  line becomes the HTTP request Claude Code would send (with the protocol
  headers the endpoint checks) and each answer a line; an answer refused
  before it was read (a wrong access code) still goes to its request.
  `BridgeSocket.cpp` sends it to 127.0.0.1 (Winsock, or BSD sockets for the
  tests). On Windows it reads the port and the access code from
  `HKEY_CURRENT_USER\Software\FamiTracker\Claude Code` for every message;
  `--port` and `--token` override them. The tests also run it against the
  endpoint over real sockets.
- `Source/McpConnection.cpp`: `SongHost` over the open document, the
  message-only window that answers on the UI thread, the settings (registry
  section "Claude Code") and the dialog. Song changes go through
  `WriteSongParts` in `InsertRecording.cpp`, one undoable action each. Paths
  the tools are given (UTF-8) reach the C library in the Windows code page
  (`SongHost::LocalPath`).
- `Source/VoiceEffectDlg.cpp`: Edit > Sound effect from your voice. It
  records with `CMicInput`, previews with `RenderEffect` through
  `PlaySound`, and adds the track through `CVoiceEffectMaker`, which
  `CMcpLink` implements with the `make_sound_effect` tool.
- `Source/Nsf/`: the NSF importer, plain C++14 built and tested on Linux
  (`cmake -S Source/Nsf -B build/nsf`). A 6502 and the sound chips'
  registers (`Cpu6502`, `Chips`), the player that records a track a tick
  at a time and finds its loop (`Player`), the conversion to a FamiTracker
  module (`Convert`: the rows, notes and instruments; `Module`: the text
  format), `Import` (all of it in one call), `Simulate` (FamiTracker's
  playback of a module, to measure how close a conversion is) and `Study`
  (a converted track described for a composer, the survey of which tracks
  are music, and studies of tracks one at a time). `nsf2ftm` runs it from
  a command line. The tests build little NSFs with their own 6502 program
  (`tests/TestNsf.h`, also used by the Api tests).
- `Source/Api/DpcmKit.cpp`: the sampled drums, synthesized (a body that
  falls in pitch, noise for the snare) and encoded as the DMC's delta
  modulation from silence; `SongHost::AddDpcmKit` adds the samples and the
  instrument that maps them to keys.
- `Source/Api/References.cpp` and `StyleGuide.cpp`: the reference library
  (references as indented JSON, searching them, a sound as a preset) and
  the built-in style guide. The library's files are read and written
  through `SongHost::ListLibrary`, `ReadLibrary`, `SaveLibrary` and
  `DeleteLibrary`; `import_nsf` opens its module with `SongHost::OpenText`,
  which `ImportNsfDlg.cpp` (File > Import NSF) shares. `study_module` reads
  a tracker module with `Source/Nsf/Tracker.cpp` (MOD, S3M, XM and IT
  patterns, followed through their breaks and jumps into a module of one
  track with a channel layout of its own) and studies it with `Study.cpp`.
- `Source/ChipSwitch.cpp`: what `set_expansion` does to the patterns before
  `SelectExpansionChip`: counting the notes on the channels that go,
  emptying them in every track, and moving their `Fxx`, `Bxx`, `Dxx` and
  `Cxx` to the last channel (a free effect column on the row; a frame whose
  pattern other frames play differently gets its own). `Source/Tests`
  checks it against the stand-in document.

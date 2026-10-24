# Claude Code in FamiTracker

FamiTracker can let [Claude Code](https://claude.com/claude-code) work on the
song you have open: read it, write parts, change the key, the tempo or the
sounds, check it for clashes and play it for you. You ask in plain words
("make the atmosphere darker"); Claude works in bars, beats, note names and
instruments, not hex rows.

It works over MCP (the Model Context Protocol). FamiTracker itself is the
MCP server, so there is nothing else to install besides Claude Code. Claude
Code in WSL connects through `famitracker-mcp.exe`, which comes in the same
zip (see [Claude Code in WSL](#claude-code-in-wsl)). Other MCP clients on
another computer of your home network, such as an AI assistant on a Linux
PC, can connect too once you allow it (see
[Connecting from another computer](#connecting-from-another-computer)).

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

## Connecting from another computer

FamiTracker can serve its tools to another computer on your home network
too: an AI assistant on a Linux PC in the next room, say, or a laptop. It's
for your home network and Tailscale only, never for the internet.

### Turning it on

1. In FamiTracker: **File > Connect Claude Code...**, tick **Let Claude Code
   connect**.
2. On the right, under **Other computers on your network**, tick **Allow
   connections from other computers on my network**. The column shows this
   computer's addresses and the access code, each with a **Copy** button:

   ```
   Ethernet:     http://192.168.1.10:46600/mcp        [Copy]
   Tailscale:    http://100.101.102.103:46600/mcp     [Copy]
   Access code:  1f3a...                              [Copy]
   ```

3. Press OK. Windows asks once whether FamiTracker may use the network
   ("Windows Defender Firewall has blocked some features of this app"): tick
   **Private networks** and press **Allow access**.
4. On the other computer, give its MCP client the address and the access
   code, as the header `Authorization: Bearer <code>`.

Take the address of the network both computers are on: the home network's
(192.168..., 10..., 172.16-31...) at home, Tailscale's (100...) wherever
your Tailscale reaches. This computer's name works too, where the other
computer can look it up (`http://desktop-pc:46600/mcp`, or Tailscale's
`http://desktop-pc.your-tailnet.ts.net:46600/mcp`). FamiTracker remembers the
setting; untick it and FamiTracker listens on 127.0.0.1 only again, as
before.

### A small set of tools for local models

Local models (a Qwen running in llama.cpp, say) do better with fewer tools
and shorter instructions than the 50 or so at `/mcp`. `/mcp/core` serves 18
of them, the composing essentials: `get_song`, `read_notes`, `write_notes`,
`add_instrument`, `design_instrument`, `set_instrument`,
`add_chords_and_bass`, `add_drums`, `add_echo`, `set_tempo`, `polish`,
`mix`, `check_song`, `review_arrangement`, `play`, `stop`, `save_song` and
`export_for_game`. Its instructions are a page instead of four, but keep the
craft (plan a key, tempo and form; a motif; chords and a bass in a style;
drums with fills; an echo; polish, then mix) and the rules that decide how
it sounds: notes on a sound that's gone at once vanish, a vibrato on the
2A03's pulses can click, and only 0.4.6's effects play. It has no recipes,
so a client adds no tools of its own to list them. Tick **A small set of
tools, for local models** in the dialog and its addresses end in
`/mcp/core`. Both sets are always there; each client picks one by its
address.

### Hermes Agent

[Hermes Agent](https://hermes-agent.nousresearch.com/docs/user-guide/features/mcp)
keeps its MCP servers in `~/.hermes/config.yaml`, under `mcp_servers`. Over
HTTP, with the address and access code from the dialog:

```yaml
mcp_servers:
  famitracker:
    url: "http://192.168.1.10:46600/mcp/core"
    headers:
      Authorization: "Bearer 1f3a..."
    timeout: 600          # export_for_game renders the song first
    connect_timeout: 30
```

Then `/reload-mcp` in a Hermes session (or start a new one). The tools show
up as `mcp_famitracker_get_song` and so on. To keep the code out of the
file, put `FAMITRACKER_TOKEN=1f3a...` into `~/.hermes/.env` and write
`Authorization: "Bearer ${env:FAMITRACKER_TOKEN}"`. Use `/mcp` instead of
`/mcp/core` for every tool, with a model that copes with them. Hermes in a
Distrobox container shares the host's network, so the same address works
there.

For clients that only start MCP servers over stdio, the zip has
`famitracker-mcp-linux` (Linux, x86-64, linked statically, so it runs in any
distribution or container). Copy it to the Linux computer, make it
executable (`chmod +x famitracker-mcp-linux`) and let the client start it:

```yaml
mcp_servers:
  famitracker:
    command: "/home/you/bin/famitracker-mcp-linux"
    args: ["--host", "192.168.1.10", "--token", "1f3a...", "--core"]
    timeout: 600
```

`--host` takes the address or the computer's name, `--port` another port
than 46600, and `--core` picks the small set (leave it out for all the
tools). It passes each message to FamiTracker over the network, like
`famitracker-mcp.exe` does for WSL, and says what to check when FamiTracker
doesn't answer.

### Files between the computers

`study_nsf`, `import_nsf`, `study_module` and `make_sound_effect` (a voice
recording) read files on the PC FamiTracker runs on. From another computer,
send the file first: `send_file` takes its name and its bytes in base64 and
puts it into the **Inbox** folder next to FamiTracker.exe
(`Documents\FamiTracker inbox` where FamiTracker's folder can't be
written); the reply gives the path to pass on, like
`C:\Users\you\FamiTracker\Inbox\Mega Man 2.nsf`. A client with a shell
sends the file itself, which is quicker:

```
curl -T "Mega Man 2.nsf" -H "Authorization: Bearer 1f3a..." "http://192.168.1.10:46600/inbox/Mega%20Man%202.nsf"
```

The Inbox takes NSF and NSFe files with their `.m3u` playlists, MOD, S3M,
XM, IT and MIDI files, FamiTracker modules and WAV recordings, at most 5 MB
each; a file of the same name is replaced. Its name can't hold folders.

`export_for_game` saves its OGG files next to the song, on the PC. When it's
asked over the network, its reply ends with a link to each file:

```
curl -H "Authorization: Bearer 1f3a..." -o Town.ogg http://192.168.1.10:46600/files/Town.ogg
```

Only the OGG files next to the song can be fetched, by their names.

### Over the network, safely

- FamiTracker only answers addresses of home networks (10.x, 172.16-31.x,
  192.168.x, 169.254.x) and Tailscale's (100.64-127.x), besides this
  computer; any other address is closed off at once.
- Every request needs the access code, files too. Web pages are turned away
  as before.
- Never forward the port on your router: from outside, use Tailscale.
- **New access code** in the dialog makes the old one stop working on the
  other computer too: give it the new one.

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
  Claude can then also set the tempo, place clips and undo. Changes leave
  the song's speed changes, jumps and halts (`Fxx`, `Bxx`, `Dxx`, `Cxx`)
  where they are, unless moving them is the change (`set_loop`,
  `insert_bars`, `remove_bars`); a clip
  Claude places keeps the ones where it lands, as when you drag one. Instruments it
  adds from presets stay in the instrument list even when the notes are
  undone. FamiTracker plays only the effect columns a channel shows, so
  where Claude's effects need more room (a vibrato beside a fade, a scoop
  beside a delay), the channel shows more of its four, as the + button over
  it would; the reply names those channels, and Ctrl+Z shows the old number
  again.

Without direct changes Claude doesn't change the tempo; when an idea needs
another one, the reply says which speed and tempo to set in the toolbar.

While a dialog is open in FamiTracker, Claude reads but doesn't change
anything; close the dialog and ask again.

### Clicks in held notes on the 2A03's pulses

The 2A03's two pulses (and the MMC5's) restart their wave whenever
FamiTracker writes a new high byte of a note's period, which it does each
time that byte changes. A vibrato (an instrument's built-in one, or `4xy`)
that takes a held note's period across a multiple of 256 therefore clicks
every time it crosses, several times a second: A-3 (period 253) does with
the gentlest vibrato, F-1 (1280) with any, and A-2, D-2 and A-1 with a deep
one. The VRC6's pulses and the triangle don't restart their waves.

- When Claude changes the song directly, a note it writes that would click
  is nudged a period step or two sharper or flatter with `Pxx` (a few
  cents, at most 15), and the next note gets the fine pitch back (the
  loop's first note too, when the nudged note is the song's last). The
  reply names the notes, for example "A-3 at bar 5 beat 1 (P81, 7 cents
  sharp)". A clip isn't nudged, since its neighbours are the song's once it's
  placed; the reply says which of its notes would click.
- `check_song` lists the notes that click: ones written before this, or
  ones an instrument's vibrato reaches after it changed. `polish` with the
  touch `clicks` nudges them, and `export_for_game` says when a track it
  exported still clicks.
- A vibrato too deep to nudge clear is named in the reply, so a gentler one
  or another channel can take the note.

### Notes that vanish

A note on a sound that's gone within a few frames (a pluck falling 15 10 5
2 1 0) comes out as a blip. Among held notes of the same line it sounds like
a missing note with a tick in its place: a user heard it at once in a tune
that went from a held lead to such a pluck and back. A tune's notes need a
sound that lasts, such as a pluck that settles to a level
(`design_instrument` kind `pluck` with sustain `half`: 15 down to about 9) or
a click pluck that holds low.

- `check_song` lists the notes of a line that vanish between its held
  notes: a run of up to four, with a held note of the line just before and
  just after, less than an octave away. A stab an octave off the line (an
  oom-pah's), a longer run (ticks, a ratchet) and a run where a line starts
  or ends are figures of their own and left alone, and so is a sound struck
  again while the note lasts (an envelope that loops back up, an echo).
  `check_song` lists the faults heard worst first (notes that vanish,
  clicks, clashes), so a long list of notes outside the key can't hide them.
- `review_arrangement` counts such notes in the melody against the score,
  and a melody whose notes mostly vanish too.
- `design_instrument` and `reference_sound` say so when the sound they add
  is gone within four frames.

### The checks in FamiTracker, and as data

FamiTracker runs `check_song` by itself too: **View > Problems** lists what
it finds in the selected track while you work by hand, and marks it in the
pattern editor and the piano roll (see `ROADMAP.md`, "Problems panel").

`check_song` with `format` `json` gives its findings as data, for programs
(the panel reads it): `track`, `track_name`, `from_bar`, `to_bar`, `key`
(and `key_from_notes` or `key_chosen`), `pattern_length`, `rows_per_beat`, `rows_per_bar`,
`counts` (the text's: `vanishing`, `click`, `clash`, `range`, `silent`,
`busy` and `key`, notes that vanish counted by the note), `unused_channels`
and `findings`, heard worst first as in the text. Each finding has:

- `kind` (as in `counts`) and `severity`: `high` for notes that vanish,
  clicks and clashes, `medium` out of range, `low` for the rest;
- `channel` (numbered from 1, as the tools take them; `null` for a silent
  bar) and `channel_name`;
- where it starts: `frame` and `row` (from 0, as the pattern editor shows
  them), `bar` and `beat` (from 1), and `position` ("bar 3 beat 2.5"), and
  `rows`, how long it lasts;
- `notes`: the notes it's about, each with `channel`, `frame`, `row`,
  `bar`, `beat`, `position`, `note` ("C#4", as written) and `rows`; a sound
  that vanishes on a channel is one finding with all its notes;
- `summary` (a few words) and `message` (the line the text gives);
- by kind: `other_channel`, `other_channel_name` and `pitches` (as they
  sound) for a clash; `clicks`, `boundary` and `nudge` for a click;
  `instrument`, `instrument_name`, `heard_frames` and `held_instrument`
  for notes that vanish; `reason` out of range; `note_count` for a busy
  bar;
- `fix`, for a click that can be nudged: the call that does it, `tool`
  (`polish`) and `arguments` (the touch `clicks` on the note's channel and
  bar), and `does`, in words. It's one Ctrl+Z.

## What Claude can do (the tools)

| Tool | What it does |
|---|---|
| `get_song` | Title, tempo in BPM, bars, the named sections and their bars, each channel and what it plays, instruments, clips, the track's key (chosen for it, or found from the notes), the chords that fit the melody bar by bar and the chords the parts play, whether the soundtrack has notes, where each part plays between the speakers (see [Stereo](#stereo)), and the channels whose fader, pan, Soft or Space in FamiTracker's mixer is off its default, with the space they send to (see [The mixer](#the-mixer)) |
| `read_notes` | One channel's notes: bar, beat, note, length in beats, instrument, volume, effects, and where a note plays when it isn't in the middle ("pan left (800)") |
| `write_notes` | Writes a part on a channel, replacing its notes in those bars (drums by name on the noise channel: kick, snare, hihat; on the DPCM channel the sampled kick, snare, low tom and high tom, each playing out with no halt after it, since 0.4.6 resets the DPCM level as it cuts a sample). A note before a rest ends with a release, or on the triangle, which can't fade, with a halt. A note can carry effects on its first row, later in it (`later_effects`: a fade into its tail, a slide) and where it ends (`end_effects`: on its release or halt, or on the next note when that starts right there). Speed changes, jumps, skips and halts (`Fxx`, `Bxx`, `Dxx`, `Cxx`) stay, so the song still loops the same way. A note's `pan` places it between the speakers (left, right, center, "half left", "30% right", a number from -1 to 1, or the effect, 840); the notes after it keep that place until one sets another, and notes without one play where the channel did there (see [Stereo](#stereo)) |
| `place_clip` | Places a clip at a bar, as a copy or linked (direct changes only) |
| `add_instrument` | Adds a sound from a preset for a 2A03, VRC6 or 5B channel, one of the VRC7's or the 5B's own sounds, an N163 wave sound or an FDS sound (see [FDS sounds](#fds-sounds)) |
| `design_instrument` | Makes a new instrument from a description: attack, decay, sustain and release, the pulse width and how it moves, a vibrato that comes in after a moment, a blip, scoop, fall, flick or thump on the attack, a chord arpeggio, the calm leads (a music box, a breath, a falling strike), and the classic NES articulations (an attack that blooms, a duty click, a decay in steps, held notes struck again, an echo after the note); or a drum, darker or brighter, shorter or longer, or a swell on the noise channel (waves, wind); on the FDS channel a wave by word and what its modulator does (see [FDS sounds](#fds-sounds)); on the VRC6 Plus's soft layer a wave sound's waves. Its reply gives the volume a frame at a time and what that means at the song's tempo, and says when a sound is gone so fast that a tune's notes on it would sound missing. See [Sounds in words](#sounds-in-words) |
| `set_instrument` | Gives a channel's notes another instrument or preset |
| `remove_unused_instruments` | Lists the instruments no note plays in any track or clip (a template's starter sounds, sounds tried and replaced) and, with `confirm` once you agree, removes them and the sequences only they used, as Edit > Clean up > Remove unused instruments does (which now also keeps the instruments clips play). FamiTracker can't undo it; `keep` names any to keep |
| `transpose` | Moves notes by semitones, or into another key or mode (C major to C minor, lydian, dorian...); arpeggiated chords follow |
| `set_tempo` | Sets the speed and tempo, from a BPM or as FamiTracker's own numbers: the track's, or from a bar on (a section's own tempo, a ritardando or accelerando over some bars) with speed and tempo effects there (direct changes only; undoable). See [Speed and tempo](#speed-and-tempo) |
| `set_meter` | Sets the meter (3/4, 6/8, 2/4, 12/8...) as FamiTracker's row highlights, which every tool counts bars and beats by; a track without notes also gets patterns of whole bars (direct changes only; undoable). See [Meter](#meter) |
| `set_swing` | Swings the rhythm: the speed alternates every half beat (or quarter beat), longer then shorter, averaging the old speed so the tempo stays; light, medium, hard or a percentage, as the speed's ticks allow; none takes it off; `by` delays swings only the channels named, with Gxx (direct changes only; undoable) |
| `mix` | Balances the parts, keeping each one's dynamics: style soft (the default) mixes like atmospheric NES tracks, the melody's loudest at 9, an echo at 4, chords at 6, a second line at 7, drums at 10, and the DPCM level at Z30 so the triangle and the noise soften too; full puts them near full volume; or levels per channel and the DPCM level (dac) alone (direct changes only; undoable). A channel that echoes the melody in some frames and plays a line of its own in others is mixed as each, there. A part's loudest counts its notes as they play: an instrument whose envelope peaks at 10 plays 10 at volume 15, and a volume slide that swells it counts as high as it goes. Volume slides (`Axy`) move with the volumes, and a swell too slow to scale stops where it would climb past the part's level, the volume column following it a step at a time. With `width` (mono, narrow, wide, full) it sets the stereo picture: the melody, the bass and the drums in the middle, an echo on the other side, chords and second lines out to the sides. See [Mixing](#mixing) and [Stereo](#stereo) |
| `set_pan` | Places a channel's notes in some bars between the speakers: left, right, center, a place between, `alternate` (note by note left and right, by `amount`) or `spread` (from left to right across the bars); the note after them plays where it did. It warns when the melody, the bass or the drums go far to one side. See [Stereo](#stereo) (direct changes only, or as a clip; undoable) |
| `add_chords_and_bass` | Chords fitted to the melody, or a progression you give by name or degree, played in a style, with a bass line in a style. See [Arrangements with depth](#arrangements-with-depth) |
| `make_groove` | A backing to start a track from, for a mood (town, overworld, battle, boss, dungeon, sad, calm, chase, upbeat, shop, underwater, castle, desert, nostalgic): chords, bass and drums, each played as that mood usually is; under a melody written first, the chords go below it and keep their added tones off its notes |
| `add_drums` | A drum part on the noise channel: a groove, small changes every other bar, a fill where each phrase ends and a crash after it. Phrases count from bar 1, or from where the loop starts when an intro plays once before it (after a 4-bar intro, a fill every 8 bars ends bars 12, 20 and 28) |
| `add_harmony` | A second voice following the melody a third (or sixth, fifth, octave) away, on the chords' notes where the melody holds |
| `add_counter_melody` | A line of its own from the chords' thirds and sevenths, holding while the melody moves and moving while it holds, below or above it; over a chord from outside the key (a secondary dominant, a diminished seventh) its passing notes keep off the key's notes that rub against the chord's |
| `polish` | Accents, a vibrato that comes in on the melody's long notes, gaps between repeated triangle notes, a nudge for notes whose vibrato clicks on the 2A03's pulses |
| `copy_bars` | Repeats bars elsewhere (A A B A), moved up or down if asked; the copy starts as the original played. The song's own jumps and ends stay where they were, and the reply names copies past the loop's end, which don't play until `set_loop` moves it |
| `insert_bars` | Puts empty bars in at a bar, in every channel: everything after moves later, and the loop's jump back (`Bxx`) and the song's end move with it, so it loops as before (bars put in right after the end play too). Whole frames go into FamiTracker's frame list, as Insert frame does, so the frames after keep their patterns and linked clips; a bar or two inside a frame moves the rows after instead. Before the loop's start, inside a frame, only whole frames go in, since FamiTracker jumps back only to a frame's start (direct changes only; undoable) |
| `remove_bars` | Takes bars out the same way: what comes after closes the gap, and the loop and the song's end move with it (direct changes only; undoable) |
| `name_section` | Names bars as one of the song's sections (Intro, A, B, Bridge, Outro), renames a section or takes its name off. A section is whole frames, so bars inside a frame widen to it, and the reply says so. See [Sections](#sections) (direct changes only; undoable) |
| `set_key` | Chooses the track's key ("D dorian", "A minor pentatonic", or "auto" to find it from the notes again), as the piano roll's Key button does; it moves no notes. See [The key](#the-key) (direct changes only; undoable) |
| `set_mixer` | Sets channels' faders (`level` in dB, -24 to +6, or `change` to add), starting pans (`pan`), `soft` and `space` (0-100) in FamiTracker's mixer, and the space they send to (`room`, `echo` in beats, `repeats`), or puts them back (`reset`); `write_into_notes` writes the faders and pans into the notes for NSF files, in every track, and sets them back. See [The mixer](#the-mixer) (direct changes only; each call one undoable change) |
| `vary_part` | Changes a repeat a little: passing notes, only the notes on the beats, syncopation, an octave up or down, an ending on the tonic |
| `review_arrangement` | How full the arrangement sounds, scored out of 10, and what would help most: missing layers, thin bars, plain or looping chords, a bass on roots, drums without fills, a melody without rests, melody notes on a sound that's gone within a few frames (they sound missing), sections pasted unchanged, flat dynamics, the stereo picture (an echo on its melody's side, a bass or drums off the middle), a melody buried under another part (counted as each chip plays, each note's instrument envelope in), parts crowding the melody's register while it sounds (a part doubling it, or trading notes with it in a hocket, doesn't), a song that keeps one loudness throughout (estimated bar by bar, each note as its instrument's envelope plays it frame by frame, so plucks die away and pads swell, the drums by how many hits they play and the DPCM level softening the triangle and the noise). A calm track (a night version, ambient music) isn't asked for drums, fills or accents |
| `soundtrack_notes` | Reads or adds to a text file next to the song that keeps your taste and the decisions about your game's music between sessions |
| `study_nsf` | Studies tracks of an NSF file you have (game music) and keeps what makes each one work in your reference library. See [References: learning from NES soundtracks](#references-learning-from-nes-soundtracks) |
| `study_module` | Studies a tracker module you have (MOD, S3M, XM or IT) from its patterns, a MIDI file from its notes, or a FamiTracker module (.ftm) track by track, and keeps it in your reference library too, so new NES music can borrow its style. See [Modules as references](#modules-as-references) |
| `references` | Lists, shows and searches the reference library, and the built-in guide to the classic NES styles (Konami, Capcom, Nintendo, Square, Sunsoft, Technos) and to dark and melancholic music |
| `effects` | FamiTracker 0.4.6's effects (what each does, on which channels, how long it lasts, what it's good for) and the techniques made with them. See [Effects and techniques](#effects-and-techniques) |
| `reference_sound` | Adds one of a reference's sounds to the song as an instrument, with the original's exact envelopes, and says when it's gone so fast that a tune's notes on it would sound missing. An FDS game's sound plays on the FDS channel with its own wave and modulator (see [FDS sounds](#fds-sounds)) |
| `find_sound` | Searches every sound the song can get in one list, as **Instrument > Sound browser** shows them: the ready-made sounds for its chips, each sound of your reference library's studies and the song's own instruments, by words ("warm bell", "pluck", a game; when no sound has every word, the ones with some), kind (lead, bass, pad, pluck, bell, keys, drums, effect) and source. A study's sounds of the same name are numbered ("Green Planet lead 2"), so each can be added. Each comes with where it's from, the channel it plays on, its words, the call that adds it (`add_instrument` or `reference_sound`) and the warnings: notes gone within four frames, a vibrato that clicks on the 2A03's pulses. With a channel, every sound that plays there |
| `import_nsf` | Opens tracks of an NSF file as a new module (the same as File > Import NSF), to look at or play them |
| `send_file` | For a client on another computer: puts a file (an NSF, a module, a MIDI file, a WAV recording) into the Inbox folder next to FamiTracker.exe and gives its path for the tools that read files. See [Files between the computers](#files-between-the-computers) |
| `add_sample_drums` | Adds a kit of sampled drums for the DPCM channel, made for FamiTracker (a kick, a snare and two toms); `write_notes` then plays them there by name |
| `add_echo` | Copies a part onto a free channel, later and quieter (it stops at the song's end, so the loop stays where it is); on another chip it plays the part's preset there, written where it sounds at the part's pitch (an octave up on the VRC7 and the 5B), and past the bars asked for it rings on only until the target's own next note. An echo note that would rub a semitone against another part is left out or cut short, unless both are tones of the chord sounding there. With `pan` "opposite" each echo note plays on the other side of its original (or give a place: left, right, half right...) |
| `add_vibrato` | Vibrato on the longer notes, starting just after each note begins: faint (`4x1`, the shallowest), gentle, medium or strong |
| `change_mood` | Several changes for a mood at once: key or mode, parts up or down, new sounds, vibrato, an echo and the tempo |
| `check_song` | Notes outside the key, clashing channels, notes out of a channel's range, busy and silent bars, notes that click on the 2A03's pulses, notes of a line that vanish between its held notes (they sound missing), unused channels. Notes count as they sound: a scoop where it lands, the triangle, the VRC7 and the 5B an octave under their written notes, the FDS two, a chord's arpeggio (an instrument's or 0xy) with all its notes, and not a fall as a note ends. N163 notes too high for the channel count are out of range: the chip plays its channels in turn, so with 4 of them a 16-sample wave skips samples (turns grainy) above A#5; so are FDS notes above G#7, where its pitch register tops out, and below A-1, which it plays under 28 Hz. `format` `json` gives every finding as data (see [The checks in FamiTracker, and as data](#the-checks-in-famitracker-and-as-data)) |
| `name_song` | Sets the module's title, author and copyright (at most 31 characters each) and the selected track's name, which `export_for_game` names its file and GameMaker sound after (direct changes only; not undoable) |
| `make_version` | Another version of the selected track as a new track: a night version, a tense one for battles, an area's variant. The original stays as it was. A night version's lush chords keep their own sounds, which a pad would lose |
| `make_jingle` | A jingle as a new track: a short tune that plays once (level up, victory, item, new quest, discovery, rest, defeat, danger), in the song's key note and sounds |
| `make_sound_effect` | A sound effect as a new track: a jump, a coin, a menu blip, wind, rain, birds and 28 more kinds, a sound described in words (built from layers), or a recording of your voice ("pew", "boing", "kssh"). Ambience loops. See [Sound effects](#sound-effects) |
| `select_track` | Switches to another track of the module, by number or name; the tools work on the selected one |
| `keep_version` | Keeps the selected track as it is now as a track of its own ("Town (version 1)", or named, like "Town (calm bridge)"), and stays on the selected track, so Claude can try something and you can compare or go back |
| `compare_versions` | Plays the same bars of two versions or more for you, one after the other (by default the newest kept version, then the track as it is now), and shows the track worked on again afterwards |
| `set_expansion` | Sets the expansion chips (any of VRC6, VRC7, FDS, MMC5, N163 and 5B, together, or none, or the VRC6 Plus), as Module > Module properties does. See [Expansion chips](#expansion-chips) and [VRC6 Plus](#vrc6-plus) |
| `check_loop` | How the song loops in a game: the intro that plays once, the part that repeats and how long each lasts, and the seam: notes hanging over it, effects, instruments, volumes, pans or tempo that differ the first time and on repeat, the chords and the melody's step across it |
| `fix_loop` | Writes what `check_loop` finds at the seam, so the repeats start as the first time does: a halt where a note from the loop's end (an echo's last note, a held chord) would hang over into a silence, and at the loop's start the instrument, volume, effects (a fade's volume slide, a vibrato), pan and speed and tempo the first time has. Only what changes nothing the first time (direct changes only; undoable) |
| `set_loop` | Sets where the loop starts (an intro before it) and ends, with a jump (`Bxx`), or with `stop` makes the song play once and stop (a halt, `Cxx`, after `to_bar` or at `stop_beat` in it); other jumps and halts go. The reply says so if the song still loops elsewhere, as when a skip (`Dxx`) ends the last frame early (direct changes only; undoable) |
| `export_for_game` | Saves tracks as looping OGG files next to the song, with the GameMaker code that plays them (the same as File > Export for games); each is brought up so its loudest peak is at -1 dBFS, since FamiTracker renders well under full scale (`normalize` false keeps its level). With `loudness` (LUFS, like -20) each track's loop is brought to that loudness instead, as EBU R128 measures it (`IntegratedLoudness` in `GameAudio.cpp`, over both channels), so a game's tracks sound equally loud; the peak still stops at -1 dBFS. The files are stereo: a song that doesn't pan has the same on both sides. FamiTracker's mixer (its faders, starting pans, Soft and Space, see [The mixer](#the-mixer)) plays in them; with Space on, the loop is the song's second time through, so the tail of its end is in the loop's start |
| `look_at_song` | A picture of the notes for Claude to look at: a piano roll of up to 32 bars, each channel in its own color, drums in a strip below, arpeggio chords as faint bars, where the song loops, rows that never play hatched, and an L or an R on notes panned to a side; the reply names the chords heard bar by bar |
| `play`, `stop` | Plays bars for you: over and over (`loop`) to listen closely, and with only some channels (`channels`) or without some (`mute`), to hear a part alone or the rest without it. Your own mutes come back when it stops (whole frames loop without a gap; other bars start again a moment after they end) |
| `undo` | Ctrl+Z (direct changes only) |
| `save_song` | Saves the song, or a copy next to it to compare versions |

Instrument presets: soft lead, bright lead, thin lead, pluck, bell, soft
chords, lush chords, warm pad, bass, thump bass (an octave blip into each
note, the punch a triangle bass can't get from volume), plucky bass, singing
lead (a vibrato that comes in by itself), strings, brass, organ, the calm
leads music box, breath and falling strike (see [Sounds in
words](#sounds-in-words)), kick, snare, hi-hat, open hi-hat, crash, tom.
Each is built for the 2A03 (also MMC5 channels) and the VRC6, and plays on
the Sunsoft 5B too. VRC7 channels play the chip's own sounds instead, and
the 5B has its own besides (see [Expansion chips](#expansion-chips)).

Notes are written the way FamiTracker shows them: C-4, C#4 (Db4 works too).
FamiTracker's octave numbers are one lower than in most music software: C-3
is middle C and A-3 is 440 Hz. Bars and beats count from 1, as the row
highlight shows them (the first highlight is a beat, the second a bar).
Claude works on the track that's selected.

### Meter

`set_meter` sets the row highlights for a meter, so a waltz counts three
beats to a bar everywhere: in `write_notes`, the chords, the drums, the
review and the loop. 3/4 keeps the song's rows a beat (4: 12 rows a bar);
6/8, 9/8 and 12/8 count dotted quarters of three eighths (6 rows a beat, an
eighth every 2 rows: the eighths of beat 1 are beats 1, 1.333 and 1.667),
and 5/8 or 7/8 count eighths. A track without notes also gets patterns of
whole bars (4 to a frame, or `bars_per_frame`); a track with notes keeps its
patterns, so the meter is set before writing (a bar that doesn't divide the
patterns is refused). In 0.4.6 the highlights are the module's, so every
track counts in the same meter. After a change to a compound meter the BPM
counts dotted quarters: `set_tempo` with bpm picks the speed for them.

### Sections

A track's frames can carry names: Intro, A, B, Bridge, Outro. In
FamiTracker, select frames in the frame editor and right-click > Name
section. Each section is a coloured band beside its frames, where the
loop's start shows too (a double-click on a band renames its section), and
the piano roll's ruler names it at its first bar. Right-click also renames
a section, takes its name off, selects its frames, moves it up or down
past the section beside it (patterns and all), and duplicates it after
itself (the copy plays the same patterns). Each is one Ctrl+Z, and
inserting, removing, moving or duplicating frames keeps the sections with
their frames.

Claude names the sections as it builds the song's form (`name_section`),
and `get_song` lists them with their bars. The tools that take bars
(`read_notes`, `play`, `look_at_song`, `copy_bars`, `remove_bars`,
`set_loop`, `set_tempo`, `add_chords_and_bass`, `check_song`...) take a
section's name instead: `play` with `section` "Bridge". `insert_bars` can
name whole frames it puts in, `copy_bars` names a whole section's copy as
its repeat ("A 2"), `remove_bars` takes a removed section's name with it,
`look_at_song` labels the sections in its picture, and `keep_version` and
`make_version` keep them. A section is whole frames, so bars moved by less
than a frame leave the sections where they were, and the reply says so.
The names are saved in the module in a block of their own (`BF SECTIONS`)
that other FamiTracker versions skip.

### The key

Each track has a key: the one chosen for it, with the Key button above
FamiTracker's piano roll or with `set_key`, or else the one its notes fit
(found as `get_song` finds it). The piano roll lights the key's notes, names
the chords the parts play in its spelling (the same chords `get_song` lists)
and, with Snap to key, lands the notes the user draws on the key's notes.
`get_song` says whether the key is chosen or found (and, for a chosen one,
what the notes alone fit), and every tool that takes a `key` uses the
track's when it isn't given one. A key moves no notes: `transpose` with
`to_key` (or `change_mood` for the whole song) moves them, and a chosen key
goes along when the whole song moves, in the same undoable change. Versions
(`keep_version`, `make_version`) keep it. A pentatonic key (a major or minor
key's five notes) keeps the piano roll to those notes; the tools use the key
itself. The choices are saved in the module in a block of their own
(`BF KEYS`) that other FamiTracker versions skip.

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

**A tempo for some bars** comes from `set_tempo` with `from_bar` (and
`beat`): speed and tempo effects (`Fxx`) there set it, and after `to_bar` the
song plays at the tempo it had (without `to_bar` the new one holds to the
end). With `over_bars` it changes gradually, a step every beat: a ritardando
for an ending that slows down, or an accelerando. The steps keep one speed
and move the tempo, and the last one lands on even rows: from 112.5 to 56.25
BPM the tempo steps down at speed 8 and ends at speed 16, tempo 150. Speed
changes inside those bars (a swing) stay, and the tempo carries the change.
Where the song loops back into the change or past it, the loop's start gets
the speed and tempo it has the first time, so every repeat plays alike.
`change_mood` with a `bpm` or `tempo_percent` for some bars writes the same
effects within its one Ctrl+Z step; for the whole song it sets the track's
tempo as before. **A song that stops** (credits, an ending, a game over) gets
a halt (`Cxx`) from `set_loop` with `stop`: on the last row of `to_bar`, or
at `stop_beat` in it, and `export_for_game` then makes a file that plays once.
FamiTracker cuts what still sounds when the halt's row comes, so a last chord
rings out or fades before it.

**Swing** (`set_swing`) works the way FamiTracker 0.4.6 composers do it: the
speed changes every half beat, longer then shorter, so the rows themselves
swing and every channel's off-beat notes come late together, notes written
later too. The two speeds average the old one, so the tempo stays: at speed
6, `F08` and `F04` in turn is a triplet feel (67%) and `F07` and `F05` a light
one (58%). The speed's ticks decide which swings fit, and Claude says which
when the one asked for doesn't; `unit` sixteenths swings quarter beats
instead, and `none` takes it off. The speed changes go on the channel that
has the song's speed effects, else the last one. Change the speed with
`set_tempo` afterwards and the swing needs setting again; a BPM keeps the
speed and changes the tempo, which keeps the swing.

With `by` delays, only the channels named swing: each of their notes on
the second eighth (or sixteenth) of a pair starts a few frames late
(`Gxx`, worked out from each row's speed), the way FamiTracker composers
shuffle hi-hats or a busy arpeggio over a straight bass; `none` with the
same `unit` takes those delays off.

## Mixing

A NES song played with every pulse at full volume sounds harsh, and the
pulses bury the triangle, which has no volume of its own. Atmospheric NES
tracks keep the pulses well down instead: a lead around 4 to 8, its echo at
2 or 3, chords at 4 to 7, and the triangle carrying the low end. `mix` does
that in one undoable step. It works out what each channel plays (the
melody, an echo of it, chords, a second line, a bass, drums) and scales
each channel's volume column so its loudest note plays at that role's
level, keeping its accents and fades. style soft (the default) puts the
melody's loudest at 9, an echo at 4, chords at 6, a second line at 7, a
bass on a pulse or the sawtooth at 10 and the drums at 10, which with the
instruments' own envelopes is about where those tracks sit; style full
goes back to near full volume; `levels` sets channels by hand. A sawtooth
well over a triangle bass (or high up, with none) plays a second line or
chords, not the bass.

A note plays at its volume times its instrument's envelope (FamiTracker
multiplies them and divides by 15), so `mix` counts each part's loudest as
it plays: a pad whose envelope holds at 10 plays 10 at volume 15, and a
melody of it mixed to 9 gets volume 14. One whose envelope peaks lower than
its level can't reach it: its volume goes to 15 and the reply says how loud
it plays. A volume slide (`Axy`) that swells a part counts as high as it
goes. The slides move with the volumes, so a fade on a part mixed to half
its volume fades half as fast from half as high; a swell too slow to slow
down further (`A10`) stops where it would climb past the part's level, and
its volume column follows the original swell a step at a time.

The NES mixes the triangle, the noise and the DPCM together, so the DPCM
channel's level (`Zxx`) also sets how loud the triangle and the noise are:
`Z00` full, `Z20` about 80%, `Z30` 70%, `Z60` half, `Z7F` 43%. FamiTracker
plays that the way the NES does. The soft mix sets `Z30` where the song
starts and where its loop starts, so nothing sticks out of the quieter
pulses; `dac` sets the level alone. Samples on the DPCM channel start from
that level, and at a high one they have less room and can clip.

Those levels are as loud as on a pulse, and the chips don't count volume
alike. Measured in FamiTracker's own rendering, a VRC7 sound at 15 plays
about 5 dB louder than a 2A03 pulse at 15, and each step down is 3 dB
(the YM2413's attenuation), so 9 there is 18 dB under 15 rather than
4 dB; the soft mix puts a VRC7 melody at 12 and its echo at 10, and keeps
each part's accents and fades as many dB under its loudest as they were.
The Sunsoft 5B's squares count volume in steps of 3 dB too, and at 15 play
as loud as a 2A03 pulse at 15 (measured: 0.4 dB louder), so the soft mix
puts a 5B melody at 13. The N163 depends on its channel count: it plays
its channels in turn, each a share of the time, which FamiTracker's mixer
makes up in part, so a square wave at 15 plays 0.6 dB under a pulse with 4
channels and 4.4 dB over with 1 (measured under Wine). Its wave sounds'
waves are rounder than a square, 4.1 dB under it on average, so with 2 to 8
channels they play about 5 dB quieter than a pulse: an N163 melody stays at
15 and its other parts sit as far under it as the roles ask. With one
channel they play about as loud as a pulse. On the VRC6 Plus, the soft
layer's parts sit another 3 dB under their roles, under the VRC6. The FDS's sounds play about 5 dB louder than a pulse with their
envelope at its top (from 4.5 to 6.5 dB, sound by sound), so its parts sit
lower. The VRC6, MMC5 and 2A03 count volume alike. The echo, the harmony and
the counter-melody take their volumes the same way (an echo three steps
under a VRC7 or 5B melody, not nine), and `polish` makes a VRC7 or 5B
part's accents one step and its fades three.

`review_arrangement` mentions a mix where every part plays near full
volume, and a melody that plays well under another part (a VRC7 melody at
9 under the triangle, say). Tools that write parts afterwards use their own
volumes, so mixing is a last step, or one to repeat at the end. A channel
with a fader in FamiTracker's mixer (see [The mixer](#the-mixer)) is mixed
as it sounds with it: 3 dB down, its volumes come out higher to match.

## Stereo

This FamiTracker plays in stereo, which the NES never did: each channel can
play from the left, the middle or the right, set by the pan effect `8xx`
(`800` all the way left, `880` the middle, `8FF` all the way right, `840`
halfway left). A pan lasts until the channel's next `8xx`, and every
channel starts in the middle, so a song without `8xx` sounds exactly as
before, the same on both sides.

Ask in words: "put the echo on the other side", "spread the chords a
little", "pan the hi-hats left and right", "make it wider for the game".
Claude places single notes with `write_notes` (a note's `pan`), a part in
some bars with `set_pan` (also `alternate` and `spread`), an echo on the
other side of its melody with `add_echo` (`pan` "opposite"), and the whole
picture with `mix` (`width` wide).

Where it helps is parts that compete: an echo on the other side of its
melody stops blurring it; chord voices or arpeggios a little apart; a
counter-melody answering from the other side; hi-hats alternating. Keep the
melody, the bass, the kick and the snare in the middle: they carry the song,
and some players hear only one speaker. Spread a little (halfway) rather
than hard left and right; on headphones a part far to one side tires.

The tools keep the pans when they write: a note written after a panned one
plays where the channel is panned, copies (`copy_bars`) and variations keep
their places, the note after a change keeps its own, and when a change
leaves the song's end panned otherwise than the loop's first note plays the
first time through, that note gets its own pan written (the reply says so),
so every repeat starts like the first time. `get_song`, `read_notes`,
`look_at_song` and `review_arrangement` say where the parts play.

You hear stereo in FamiTracker's playback, in File > Create WAV and in the
game's OGG files (`export_for_game`, whose loudness is measured over both
channels, as EBU R128 does). NSF files and the NES are mono: NSF export
leaves `8xx` out. FamiTracker 0.4.6 opens a song with pans, shows
"(invalid)" on rows with `8xx` in their first effect column and plays
everything in the middle.

In FamiTracker itself, type `8` in an effect column, or select notes in
the piano roll and pick **Effects > Pan (stereo)**: Left, Halfway left,
Center, Halfway right or Right. A panned note shows an arrow at the end it
leans to.

## The mixer

**View > Mixer** opens a strip for every channel of the song along the
bottom of the window, in channel order (the 2A03's, then each expansion
chip's that's on). Each strip has the channel's name and chip, a meter
that moves while the song plays, **M** and **S** (mute and solo, the same
as clicking the channel's name in the pattern editor), a fader from -24 to
+6 dB and a pan from left to right.

- Drag the fader (Shift for tenths of a dB) or the pan; double-click one to
  put it back at 0 dB or the middle, or right-click a strip. Each change is
  one Ctrl+Z, a drag one for the drag.
- The faders and pans are saved with the song, the same in every track.
  FamiTracker's playback, File > Create WAV and File > Export for games
  play them; they aren't changes to the notes.
- A pan is where the channel starts, before any `8xx`; an `8xx` still moves
  it from there, and every time the song starts again it starts there.
- NSF files play the notes as written, so NSF export leaves the mixer out
  and says so. **Write into the notes** (the button, or Claude's
  `set_mixer` with `write_into_notes`) puts it into every track: each
  fader into the channel's volume column, as `mix` scales a part, and each
  pan as `8xx` on the channel's first note. Then the faders go back to 0 dB
  and the middle, so the NSF sounds as the mix did, give or take a
  volume step. One Ctrl+Z undoes it all.
  - The triangle and the DPCM have no volume column: their faders stay in
    the mixer.
  - A channel raised past its loudest notes' volume 15 can't play louder
    in its notes. Then every channel comes down by that much instead,
    which keeps the balance (the song plays a little quieter).

Claude sees the mixer in `get_song`, sets it with `set_mixer`
("put the triangle 3 dB down", "start the echo channel half left"), and
counts the faders when it weighs how loud parts sound (`mix`, the review,
the echo and the harmony). For a mix that NSF files keep too, the notes'
own volumes (`mix`) are the better place; the mixer suits quick balancing
while you listen, and the game's OGG files.

### Soft and Space

Under each strip's pan are two knobs, **Soft** and **Space**, both off (0)
unless you turn them up to 100. They add a little depth that the NES never
had, so they're optional and meant for the game's OGG files: FamiTracker's
playback, File > Create WAV and File > Export for games play them, NSF files
never do (NSF export says so), and with both at 0 everywhere the sound is
exactly what it was.

- **Soft** rounds off a channel's edges with a gentle low-pass, from barely
  there at 1 to warm at 100 (about 7 dB off the sound over 2 kHz). A pulse
  or a sawtooth still sounds like that chip, never muffled: at 100 it's
  still brighter than a triangle at the same pitch.
- **Space** sends that much of the channel (after its fader) to the space
  all channels share: a reverb in a room, with an echo, coming back on both
  sides. Choose the room, the echo and its repeats at the top of the mixer:
  a small room, a hall, a cathedral, a cave, or none (the echo alone); the
  echo in beats of the playing track's tempo (off, 1/4 to 2 beats) and how
  many times it repeats (1-8, each quieter). One space for the whole song.
- Drag a knob up or down (Shift for finer steps), turn it with the mouse
  wheel, or double-click it to turn it off. Each change is one Ctrl+Z, a drag
  one for the drag; right-click a strip to turn both off.
- Use them sparingly: a little Space on pads and chords, a lead or an echo
  in a big place like a cave or a cathedral, Soft on a pulse or a sawtooth
  that should sound warm. Leave the bass and the drums dry.
- The space rings on after the notes: when a song stops, File > Create WAV
  keeps rendering until its tail has died away, and **Export for games**
  takes the loop from the song's second time through, whose start already
  has the tail of the end, so the loop's jump back keeps the echo going. A
  song that halts keeps its tail.
- Write into the notes leaves Soft and Space in the mixer: notes can't hold
  them.

Claude sets them with `set_mixer` (`soft`, `space`, and `room`, `echo`,
`repeats` for the space: "a little space on the pads in a hall", "put the
lead in a cave with a half-beat echo") and counts each channel's level
before them in `mix` and the review.

## Expansion chips

Ask for "a VRC6 song", "a VRC7 song", "a 5B song" or "VRC6 and N163" and
Claude sets the expansion chips with `set_expansion` (`chips`: a list, or
`chip`: one), as **Module > Module properties** does with its checkboxes. A
song can have any of the VRC6, VRC7, FDS, MMC5, N163 and Sunsoft 5B on at
once, for every track; their channels follow the 2A03's in the order VRC6,
MMC5, N163, FDS, VRC7, 5B. The channels of the chips that stay keep their
notes (they move to their new place); the channels of chips switched off go,
and new ones start empty:

- When the channels that go have notes, Claude asks you first, since
  they're cleared and FamiTracker can't undo switching chips (Module >
  Module properties switches back, without the notes). Save a copy first if
  you might want them. Fewer N163 channels count the same way.
- Speed changes, loops and ends (`Fxx`, `Bxx`, `Dxx`, `Cxx`) on those
  channels move to the song's last channel, so the song still loops the way
  it did.
- Expansion chips play on NTSC machines, so a PAL song becomes NTSC.
- FamiTracker's NSF export plays one expansion chip, and not the 5B, so it
  refuses a song with several chips (the VRC6 Plus is two) or the 5B,
  saying why; `export_for_game` (OGG) and WAV export play every chip.

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
which lets the sound fade the way the chip does. Like the triangle, the VRC7
sounds an octave under the note written (its A-3 is 220 Hz, a pulse's 440),
so a part that plays with a pulse is written an octave up there; the echo,
the harmony, the counter-melody, voiced chords and a jingle's harmony are
written that way, and the checks and the review count VRC7 notes where they
sound.

The chip also has one custom patch, whose 8 registers set two FM operators
(their frequency ratio, the modulation level, feedback and envelopes).
Claude has six of its own design, measured with the same emulator
FamiTracker plays: electric piano (a bright, bell-like attack that mellows
into a warm tone, for comping chords), soft electric piano, glass bell, fm
pad (swells in and holds, with a gentle vibrato), fm bass and wooden
mallet. Ask for them by name ("rhodes" and "epiano" work too). The chip
holds one custom patch at a time, so two channels playing different custom
patches at once both sound like the one struck last; Claude says so when a
song gets a second one. Two channels playing the same custom patch are
fine, which is how an electric piano comps chords across them.

The **N163** (Namco 163) adds 1 to 8 wavetable channels, 13 in all with the
2A03's, and Claude draws their sounds: soft flute, drawbar organ, string
ensemble, choir, brass section, electric piano, glass bell, harp, marimba,
pulse sweep lead, growl bass and round bass, and the VRC6 Plus's choir pad,
string pad, sub bass, breath, quick pad, pulsing pad and music box. Each
plays waves of its own one
after the other, so its timbre moves as a note plays: the organ's upper
drawbars turn like a rotating speaker, the strings and the choir shimmer,
brass opens up from mellow to bright, the electric piano and the bell mellow
as they fade, the lead's pulse narrows and widens smoothly, and the bass
growls like a wah. A preset's name picks the sound most like it ("soft
lead" is the flute, "bass" the round bass). A song holds up to 8 of them:
FamiTracker 0.4.6 loads an N163 wave only from the first 128 samples of the
chip's memory (a wave placed later makes the module load out of step), so
Claude puts each sound's 16-sample wave there, in a place of its own.

- The waves are 16 samples long: FamiTracker 0.4.6 plays N163 notes in tune
  at that length (a 32-sample wave sounds an octave lower than written).
- All waves share the chip's 128 bytes of memory with the channels'
  registers, 16 samples for each channel in use: with 6 channels there is
  room for 10 sounds, with 8 for 8. Claude puts each new sound's wave where
  no other N163 instrument's is, from the bottom up, and says how much room
  is left.
- The chip plays its channels in turn, which whines at a pitch that falls as
  channels are added: up to 6 keep it above hearing (about 20 kHz), 7 and 8
  bring it down to 17 and 15 kHz. FamiTracker plays it too, so it's in the
  exports.

### VRC6 Plus

The **VRC6 Plus** is this FamiTracker's own: the real VRC6 (its two pulses
and its sawtooth) with a soft layer of four wave channels played by the
N163, for when a VRC6 song should be a bit softer, warmer or deeper. It
plays only soft waves through the N163's emulation, so it still sounds like
the NES. Ask for "a VRC6 Plus song", or for something softer or deeper on a
VRC6 song, and Claude sets it with `set_expansion` (`chips`:
`["VRC6 Plus"]`, or with another chip beside it); **Module > Module
properties** has a box for it.

- It's one chip: its channels are Pulse 1, Pulse 2 and Sawtooth of the VRC6
  Plus, then Soft 1 to Soft 4, in the pattern editor, the piano roll, the
  mixer, the problems panel and the tools ("VRC6 Plus Soft 1"). The tools
  still take the soft channels' N163 names ("Namco 1", "N163 Namco 1") and
  the pulses' VRC6 ones.
- The VRC6 carries the music (the melody, the sawtooth's bass and bite);
  the soft layer sits underneath, quieter and optional: a pad or a choir
  holding the chords (`add_chords_and_bass` with `chord_style` `voices`
  puts them on the soft channels, the string pad by default), a soft echo
  (`add_echo` onto Soft 1 with the soft flute), a sub bass an octave under
  the sawtooth. Left empty, the song is plain VRC6. `mix` keeps the soft
  layer 3 dB under its parts' roles. The style guide's topic `vrc6-plus`
  (`references` with action `guide`) has more.
- Choosing it adds what's missing of its starter sounds, each with a wave
  of its own under sample 128: choir pad, string pad, soft flute and sub
  bass. `find_sound` and the sound browser list them first, under "VRC6
  Plus soft layer", then its calm shapes, then the N163's other sounds.
- Its calm shapes, added by name on a soft channel (a song holds 8 soft
  sounds, the starters among them): **breath**, a breathy lead that comes
  in from 4 over 11 frames, sinks to 10 and holds, with a faint vibrato;
  **quick pad**, in within 7 frames, for a chord every beat (the starter
  pads take 20-24 frames to swell); **pulsing pad**, struck again every 12
  frames, each strike softer down to one it repeats, with a soft echo after
  a release (the slow pads of atmospheric FamiTracker music); **music box**, a bright tine that
  mellows to a pure tone and is gone in 40 frames.
- How loud they play at volume 15 against a 2A03 pulse at 15 (measured,
  with the soft layer's 4 channels): choir pad 8.3 dB under, string pad
  7.3, soft flute 5.3, sub bass 5.1, breath 7.4, quick pad 7.7, pulsing
  pad 10.4 over its strikes, music box 4.8 at its strike. `add_instrument`
  says it when it adds one.
- `design_instrument` makes sounds of your own there: a wave sound's waves
  (`wave`, or the kind's: the soft flute's for a lead, the string pad's for
  a pad, the breath's and the music box's for theirs) with the envelope,
  vibrato and pitch you describe, each with a wave place of its own.
- Underneath, the song is VRC6 + N163 with 4 channels, marked by a
  `BF VRC6PLUS` block, so other FamiTrackers open it as that and play it
  the same. Back to the VRC6 (`chips: ["VRC6"]`) clears the soft channels
  after asking; `["VRC6", "N163"]` keeps them as plain N163 channels.
  NSF export plays one chip, so a VRC6 Plus song goes to a game as OGG
  files.

The **Sunsoft 5B** adds three square waves (always 50%), each of which can
also play the chip's noise and its envelope. Claude has sounds of its own for
it: square lead, singing square, square pad, square pluck, square bell, buzz
bass, sync buzz, noise hat, noise crash, noise snare, square kick and square
tom, also in **Instrument > Sound browser**. The presets play there
too ("soft lead", "warm pad"), their volume envelopes turned into the 5B's
3 dB steps so they sound as loud as on a pulse; "snare", "hi-hat" and
"kick" there are the 5B's own drums. Like the triangle and the VRC7, the 5B
sounds an octave under the note written, and the tools that write parts
for it allow for that.

- The buzz bass plays the chip's envelope as a falling saw at the note's
  pitch: a strong, bright bass. Until a song sets the envelope with `Hxy`
  (`y` the shape, `x` how its speed follows the note: `C` at the note's
  pitch), `Ixx` and `Jxx` (its period), envelope notes play `HC8`. The
  envelope sets the level, so the volume column only silences it (at 0),
  and the three channels share the one envelope: one buzz at a time.
- An instrument's duty sequence is the 5B's noise and mode a frame at a
  time: 64 the square, 128 the noise, 32 the envelope, added up, and the
  noise's pitch (0-31) in the low bits; `Vxx` sets the mode for notes
  without one (1 the square, 2 the noise, 4 the envelope).

### FDS sounds

The **FDS** (the Famicom Disk System) adds one wavetable channel whose
modulator bends its pitch, and Claude draws its sounds: soft flute, whistle,
eerie lead, glass bell, warm organ, nasal reed, swelling strings, choir pad,
round bass, growl bass, underwater wobble and cave drone. Each is a wave of
64 samples, a volume envelope (the FDS counts 0-31) and, where it makes the
sound, a vibrato or the modulator:

- The flute, the whistle, the reed, the strings and the choir sing with a
  vibrato that comes in after a moment, and the organ has a faint chorus.
  The strings and the choir swell in; the cave drone breathes slowly in and
  out as it holds.
- The eerie lead holds its note for half a second, then its pitch trembles
  widely five times a second, like a theremin in the dark. The underwater
  wobble wobbles three times a second from the start.
- The growl bass's modulator shakes its pitch 35 times a second, so it
  growls; the cave drone's 26 times, a faint rumble.
- The glass bell strikes two octaves and then one octave above its note,
  a frame each, rings and dies away over a second and a half.

Ask for them by name, or by a preset's name, which picks the sound most like
it ("soft lead" is the flute, "bass" the round bass, "warm pad" the choir).
**Instrument > Sound browser** offers the same sounds for the FDS
channel. `design_instrument` makes new ones there from words: a wave (sine,
round, hollow, square, bright, strings, nasal, organ, voice, glassy, bass,
triangle), the envelope, attack and vibrato as for the other chips, and what
the modulator does (wobble, tremble, growl or rumble, from a moment into the
note if asked).

Measured in FamiTracker's own rendering against a 2A03 pulse at 15 playing
the same notes where they sound, they play about 5 dB louder with their
envelope at its top, so `mix` sets FDS parts lower. A wave of many
harmonics in phase peaks high with little power in it, so the organ, the
reed, the strings, the choir, the basses and the drone have their harmonics'
phases picked for the most power: up to 5 dB louder at the same volume, the
same harmonics.

- FamiTracker 0.4.6 plays the FDS two octaves under the note written: its
  A-5 sounds 440 Hz, as a pulse's A-3 does. Every FDS instrument does,
  also the ones made in FamiTracker's instrument editor, whose waves have
  one cycle as these do. Claude writes FDS notes two octaves up, the echo,
  the harmony, the counter-melody, the chords and the bass do so by
  themselves, and the checks count FDS notes where they sound.
- The FDS plays up to G#7 (sounding G#5): its pitch register tops out
  there, so A-7 plays 13 cents flat and A#7 and B-7 no higher. `check_song`
  lists such notes, and ones under A-1, which sound under 28 Hz.
- 0.4.6's modulator rounds each bend away from the note, in 64ths of its
  pitch: the smallest bend it makes is about 55 cents down or 79 up,
  whatever the depth (FamiTracker's own "sine" table at depth 1 wobbles
  between the note and a quarter tone under it, so the note sounds flat).
  So these sounds' vibratos are pitch sequences, and their wobbles take
  turns between those two smallest bends, which stays in tune on average.
- `Hxx`, `Ixx` and `Jxx` on the FDS channel set its modulator's depth and
  speed as a part plays.

A study of a Famicom Disk System game (`study_nsf`, or `study_module` on an
FDS module) keeps each FDS sound's wave and modulator with its envelopes,
and `reference_sound` makes the instrument again on the FDS channel as it
was (in an FDS song also when no channel is given). On a pulse an FDS
sound's volume is counted 0-15 from the FDS's 0-32, its pitch sequence in
the pulse's coarser steps, and it plays the duty closest to how bright its
wave is. Another chip's sound on the FDS channel plays its duty's pulse, a
sawtooth or a triangle as its wave, its volume counted 0-31. References
studied before the waves were kept give a round wave: study the NSF again
for its own.

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
  waltz...), `broken` chords (up, down, updown, alberti, rolling), a
  `guide` line (one long note a chord on its third or seventh), or `lush`
  chords: an instrument for each chord whose arpeggio loops through four or
  more of its notes, two frames each (`arp_frames`), so sevenths and ninths
  ring, played softly (volume 6) and colored rich unless asked otherwise.
  Their instruments ("Lush maj7", "Lush m9") are made from a preset's sound
  (lush chords by default) when first needed; `pattern` turns their
  arpeggios down, up and back, leaping (alberti) or spread over two octaves
  (rolling). They need a 2A03, MMC5 or VRC6 pulse or a 5B channel, since
  VRC7 instruments have no arpeggios.
  Or `voices`: the chord's notes on 2-4 channels of their own
  (`chord_channels`, else free FM channels, then N163 ones, then 5B ones,
  then pulses),
  the third and seventh first, each voice moving to the next chord's
  nearest note without crossing: an electric piano comping on the VRC7's
  FM channels (its custom patch), held or struck on a rhythm, each voice a
  frame after the one below it (`strum`, `Gxx`) as a hand rolls a chord.
  Chords asked for over bars that have chords and a bass line already (other
  chords for a section) take their place, on their channels (voiced chords
  on the voices' channels), rather than stacking a second part on them.
  Chords given for more bars than the melody plays (an intro of chords
  before it) start early enough to end with the melody.
- **Bass** (`bass_style`): root_fifth, roots, half, octaves, walking (with
  approach notes into each chord), syncopated, arpeggio, driving, pedal. On
  the triangle it sits where it sounds like a bass (the triangle plays an
  octave below what's written).
- **Drums** (`add_drums`): basic, four_on_floor, half_time, shuffle, march,
  breakbeat, calm, driving, waltz; light, normal or busy; an open hi-hat into
  every other bar, fills that take turns (toms, a snare roll, kick and snare)
  and crashes. With `kit` samples (also in `make_groove`) the kick, the snare
  and the fills' toms are the sampled drums of `add_sample_drums` on the DPCM
  channel, and the noise channel keeps the hi-hats on the rows where it would
  otherwise give them up to a kick or a snare; `mix` writes its DPCM level on
  each sample, which would otherwise start it from silence. Each sample ends
  at the level it started from, whatever level that is, so the hits don't
  click and the bass doesn't jump between them (the toms of the first kit
  slid to silence when they started higher; a song with that kit has its
  samples brought up to date when the sampled drums are used again).
- **A second line**: `add_counter_melody` or `add_harmony`, quieter, on a
  free channel.
- **Finishing touches**: `polish` (accents, a vibrato that waits, gaps
  between repeated triangle notes, a nudge for notes that click; when asked, fades on held notes, `A0y`
  from halfway through, scoops into held notes after a leap up, falls (a
  sigh of two semitones, `R42`) where phrases end, and a staccato bass that
  bounces);
  `make_groove` starts a whole backing for a mood; `copy_bars` and `vary_part` build a form whose repeats change,
  and `insert_bars` and `remove_bars` make room for a section or cut one; `name_section` names the parts as
  the form grows (Intro, A, B, Bridge), and the tools then take a section's name in place of bars.

`review_arrangement` listens the way an arranger would and scores the depth
out of 10, with what would help most and the tool for each. Claude runs it
before calling a track done.

A track also needs to breathe. Rendered side by side, well-arranged NES
tracks open 8-10 dB under their main sections, drop for a breakdown and
come back, where a track whose parts all play every bar keeps one loudness
from start to end. The review estimates each two bars' loudness from the
parts that sound in them, at their volumes as each chip plays them, and
says how far the song swings; under 3 dB it asks for an intro and a break.
The triangle is loud, so bars with the bass stay loud: an intro on the
chords alone plays about 10 dB under the rest, with the bass only 3.
`compose_track` builds that in: an intro of a few bars with one or two
quiet parts (`insert_bars` at bar 1, `copy_bars` with `channels`, and
`set_loop` from the bar after, so it plays once), and a few bars without
the drums or the melody before a repeat. An intro makes the loop's seam
matter: a fade or an echo at the loop's end carries into its start on
repeat, where the first time came from the quiet intro, so the recipe ends
with `check_loop` and `fix_loop`.

## Sounds in words

`design_instrument` makes an instrument from words, starting from a kind
(lead, pad, pluck, bass, keys, bell, brass, strings, organ, chiptune; kick,
snare, hihat, open hihat, crash, tom) and changing what should differ:
attack, decay, sustain and release (words or frames, 60 a second; a decay of
40 frames reaches its sustain level, or silence, on frame 40; the decay's
words go by the kind, so a long bell rings twice its usual 40 frames), the
pulse width (12.5%, 25%, 50%; on the VRC6's pulses every 6.25% up to 50%,
such as the 37.5% of Castlevania III's night leads) and whether it opens
up, shimmers or thins as it fades, a vibrato built in that comes in after a
delay given in beats (faint, gentle, medium or strong: about 3, 20, 40 and
60 cents either way at A-3), an octave blip, a scoop or a fall on the
attack, or a chord played as a fast arpeggio from every note (major, minor,
sus2, sus4, 6, m6, maj7, m7, 7, add9, m(add9), diminished, m7b5, dim7,
augmented). A sound that holds under its peak (a pad at half) swells to that
level, or settles to it, rather than dropping straight after its attack.
Drums can be darker or brighter, shorter or longer (a hi-hat sits at the
noise's D-#: at E-# most of its sound lies above hearing). The instrument
gets a new name, so notes that use another one don't change. The reply
gives its volume a frame at a time and what that means at the song's tempo
("down to half (7) on frame 16 and silent from frame 43, 0.9 of a beat"),
and on the 2A03's pulses the notes its vibrato would click on.

Three kinds are calm leads, the shapes of the quiet tracks in the
reference library, none of which holds a high level from attack to
release (a level held like that presses, however soft the parts around
it). Their times follow the song's tempo, and a volume given scales their
levels:

- **music box**: struck at its volume and gone in 0.9 of a beat (43 frames
  at 75 BPM, half gone by frame 16), 50%, like a music box or a piano.
- **breath**: breathes in from 4 to 15 over 12 frames, sinks to 9 by frame
  22 and holds it, with a faint vibrato half a beat in, and fades over 24
  frames after a release; 50%.
- **falling strike**: struck at 15, falls to 6 over 14 frames and holds it
  there, the faint vibrato half a beat in, a short fade after a release;
  25% (Final Fantasy III's Time Remains).

They're presets too, by the same names. The faint vibrato is theirs: the
period a step flatter for half of each 12 frames, which takes no note of
the 2A03's pulses across a multiple of 256, so it never clicks.

On the noise channel, a kind that isn't a drum makes a volume envelope: a
`swell` rises over half a second and washes out after a release, for waves
and gusts of wind (the note picks the noise's pitch, 0-# deep to F-#
bright). Anything that would move its pitch or make it metallic (vibrato,
an attack's pitch, an arpeggio, a duty) is left out. A pulse sound played on
the VRC6 sawtooth overdrives it (the odd values of its duty sequence add 32
to the sawtooth's volume), so a sound asked for there by name plays its
sawtooth version when the song has one ("Horn (saw)", made by
`design_instrument` or a preset for that channel), and Claude says so
otherwise.

The articulations the studied NES soundtracks are made of are there too:
an attack that blooms from about two thirds of its peak in two frames, a
duty click (each note's first frame at another pulse width), a decay in
steps, held notes struck again every so many beats, a soft echo after the
note, a quick pitch flick, and a bass that thumps in from a fourth above.
The kinds "bloom lead", "stepped lead", "click pluck", "echo pluck" and
"thump bass" start from them. A held note struck again dips over two frames
to about half its level (at most 5 under) and strikes a little over it, on
the beat's grid from the note's start; a sound that dies away (a pluck, a
music box) is struck again from its peak until a release. Drums and the
triangle can't be struck again that way, and the reply says so.

A sound gone within four frames (a pluck with a short decay and no
sustain) is for echoes, stabs and drums, and the reply says so: a tune's
quick notes on it come out as blips (see [Notes that vanish](#notes-that-vanish)).

## Effects and techniques

Claude knows FamiTracker 0.4.6's effects, and only those: `effects` says
what each one does, which channels play it, whether it stays on until turned
off with `00` and what it's good for, and how the classic techniques are
made: an echo channel, a chorus with `Pxx` (P81 on a doubled part), vibrato
that comes in after a beat, swing with `Gxx` on the off-beats, staccato with
`Sxx`, fades and swells with `Axy`, tremolo, the 2A03 pulses' hardware sweep
(`Hxy`, `Ixy`), duty changes with `Vxx`, slides and grace notes, chords as
arpeggios, a kick on the triangle, DPCM tricks and noise drums.

When Claude writes notes, an effect FamiTracker 0.4.6 doesn't have (`Lxx`,
`Mxy`, `Oxx` and `Txy` come from later FamiTrackers; `5xy`, `8xx` and the
like from MOD and XM) or one the channel can't play (a sweep on a VRC6
pulse, tremolo on the triangle, `Vxx` beyond what a channel takes, `Gxx`
longer than a row) is refused with what to use instead, so nothing silently
does nothing. On the FDS channel, `Hxx`, `Ixx` and `Jxx` are its modulator.

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

Each study is kept as a reference in a library of yours, the `References`
folder next to FamiTracker.exe (a JSON file each), for every song and every
session, so it moves with FamiTracker. References kept in
`Documents\FamiTracker references`, where the library used to be, move into
it by themselves, and so does a `FamiTracker references` folder unpacked next
to FamiTracker.exe. Where FamiTracker's folder can't be written (under
Program Files), the library stays in Documents. When you ask for music "like Bucky O'Hare", "in a Mega Man
style" or "lean on the references", Claude searches the library
(`references`) and plans from what it finds, and `reference_sound` brings
in a reference's instrument exactly. The melodies aren't kept: Claude
takes the craft (grooves, bass figures, harmonic color, how the channels
share the parts, the sounds) and writes its own tunes and progressions.
Built in, without any NSF, is a guide to the classic NES styles from
studies of Konami's (Bucky O'Hare, TMNT II, Castlevania, Castlevania III,
Super C), Capcom's (Mega Man 2-5, Little Nemo, Strider), Nintendo's (Super
Mario Bros. 2 and 3, The Legend of Zelda, Kid Icarus on the Famicom Disk
System, Gyromite, Punch-Out!!, Dr. Mario), Square's (Final Fantasy III),
Sunsoft's (Journey to Silius) and Technos's (Nintendo World Cup)
soundtracks. Its `dark` topic is the craft of dungeons, caves, crypts and
the underworld in all of them (low, narrow registers, semitones and
tritones, pedals and ostinatos, music without drums, quiet echoes), and
`melancholy` that of sad scenes, laments and night. Its
`soundtrack` topic is about a game's music as a whole, from composing a
30-track soundtrack with these tools: a catalogue of places, a leitmotif
that comes back changed, tempos whose rows all last the same, a loudness
for each kind of place, and forms for towns, dungeons, bosses and the
credits.

You can tag references and keep notes with them ("the user loves this
bass"), and `import_nsf` (or File > Import NSF) opens tracks as a module to
look at the notes themselves. `compose_track` looks up a composer, a game
or a reference named in its idea or in the soundtrack notes by itself, so
"a save room in the style of <a composer whose modules you studied>" plans
from those studies. A composer is found by the name the modules give as
their author.

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

FamiTracker modules (`.ftm`, from 0.2 to 0.4.6) are studied a track at a
time from their own patterns and instruments, like "study
C:\Users\Name\Music\song.ftm": besides what any study finds, the speed and
tempo as written (and a swing, or a lilt between two speeds, from speeds
that alternate), echoes that run only in some sections, chords played by
arpeggio instruments, notes delayed with `Gxx`, duty changes with `Vxx`,
detune with `Pxx`, the DPCM level written with `Zxx` and each channel's
volumes. Their sounds are kept with their sequences as written, so
`reference_sound` gives a song one of them.

MIDI files (`.mid`, `.midi`, `.rmi`) are studied the same way, like "study
the town theme in C:\Users\Name\Music\town.mid". Their notes go on a grid
of rows (4 a beat, or 6, 8 or 12 for triplets and thirty-seconds), and
their bars follow the file's meter, so a waltz is counted in 3/4 and a jig
in 6/8 (two beats of three eighths). Each part becomes channels of one note
at a time: a piano's chords spread over several, the highest note on the
first, so a melody stays on one channel; the drums (MIDI channel 10) go on
a channel for each kind (kick, snare, hi-hat, cymbal, tom, percussion),
named after General MIDI's drums. The channels are named after the file's
tracks, or after their General MIDI instruments, and a track called
"Melody", "Lead" or "Soprano" is taken as the lead. The tempo is the one
the file keeps longest, and a loop point (controller 111 as RPG Maker
writes it, EMIDI's loop controllers, or `loopStart`/`loopEnd` markers)
makes it loop. Grace notes are left out. A MIDI study is tagged `midi`, and
with its meter when that isn't 4/4.

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

Night is much more than slower. The night version is, unless you ask for
something else:

- lower: the parts that sat high (from C-4 up, not the bass) an octave down,
- simpler: the melody's fast runs thinned to their outline (the short notes
  between the eighths left out, the notes before ringing on),
- legato: notes held into the next (no note cuts; a halt just before a note
  goes, the others soften to releases), with even dynamics, no accents,
- with chords held as a pad that swells in (a chord struck again in its bar
  rings on instead; ask for "richer chords" to turn triads into sevenths),
- with the bass in long notes (the notes on the beats, held),
- with the drums a soft pulse on each bar's first beat (sampled drums on the
  DPCM channel thin out the same way), or none if you ask,
- with a soft lead that swells in for the melody and a warm pad for
  arpeggio chords, a slower and wider vibrato starting later in the note,
  and a longer, softer echo (no second echo when the song already has one),
- 20% slower (tempo commands in the patterns slow down too) and quieter
  (every channel's volume at 75%).

Claude says what it changed. Say what you want different, in words: "keep
the drums", "only 10% slower", "no echo", "keep the melody as it is", "in
dorian". Claude can also change the night version afterwards with the other
tools, since it's the selected track.

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
| wind, rain, fire | ambience: a heavy wind blowing in gusts, rain pattering, a fire crackling (these loop) |
| thunder, birds, wings | a crack of thunder rolling away, two birds chirping, a bird flying off ("bird flying away") |
| engine, alarm, heartbeat | an engine idling, a siren, a heartbeat for low health (these loop) |

Every kind has variants (variant 2, 3, ... are a little higher or lower,
shorter or longer, some with another duty), and Claude can move a sound by
semitones, stretch it, or make it softer. A sound lasts at most 4.2 seconds.

**Ambience loops** instead of halting: its track ends with a jump back to
its start (`B00`), and what reaches its end (a gust, a flutter) goes on into
its start, so the export for games saves an OGG that loops without a seam
(stop it with `audio_stop_sound`). Any sound can loop if you ask, and a kind
that loops can play once instead.

**Sounds described in words.** For anything else ("a bird flying away", "a
heavy wind through a ruin", "a robot powering down"), Claude builds the
sound from layers: a part each on a pulse, the triangle or the noise
channel, with when it sounds, its pitch and how it slides, its loudness and
how it moves (a hit, a fade, a swell), repeats (chirps, flaps, beeps) and
the textures of nature: gusts, a flutter, crackle, drops. Layers that don't
sound at once share a channel. The reply lists the layers in words, so you
can ask for one to change ("the flaps slower", "the chirp higher"), and a
variant of a described sound also draws its gusts, crackle and drops anew.

Claude can also turn **a recording of your voice** into a sound effect: give
it the path of a WAV file ("C:\Users\Me\Documents\pew.wav"). Where your
voice has a pitch, a pulse follows it, sliding between notes, moved by whole
octaves so it sits where NES sound effects do; the breathy and hissy rest
plays on the noise channel, brighter for "sss" than for a breath. Silence
before and after is left out, and a sound can last up to 4.2 seconds.
FamiTracker can do this without Claude, too: Edit > Sound effect from your
voice records the sound, plays it as the NES would and adds it with this
same tool (see `RECORDER.md`). Its takes stay in the `Recordings` folder
next to FamiTracker.exe, so you can ask Claude for another version of one
("the last voice take, an octave lower": the recording `"latest"` is the
newest take).

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
  starts the way every repeat does. `check_loop` tells you which, and why;
  `fix_loop` makes the loop start the same way both times where it can.
- The loop points move together by up to 2 ms to where the waves meet, so
  the jump doesn't click; the loop keeps its exact length.
- A song that sets the DPCM level on its first row (as `mix` does) steps
  the DMC's output up from silence there, a pop that the repeats don't
  have. The file fades in over its first 40 ms, before its level is
  measured (the pop used to be its loudest peak and kept the rest quieter),
  and a song that loops as a whole repeats from its second time through,
  so the fade stays out of the loop.

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

**Loudness** picks how loud the files come out, and is remembered:

- "As loud as it goes (peak at -1 dB)", the default: each track is brought
  up until its loudest peak is at -1 dBFS.
- "Calm place (-22 LUFS)", "Most tracks (-20 LUFS)", "Action and bosses
  (-19 LUFS)", or "Other..." with a number from -40 to -6: every track
  exported (with All tracks, all of them) is brought to that loudness, as
  EBU R128 measures its loop over both channels (the `loudness` of
  `export_for_game`).

Tracks at the same loudness sound equally loud in the game, one after
another; tracks brought up to their loudest peak don't (a quiet track with
one loud hit stays quiet). At a loudness, a track's loudest peak still stops
at -1 dBFS: a track it holds back comes out a little quieter than the
others, and the result says so (a lower loudness for all of them, or that
peak softened, makes them match).

The song has to be saved, since the files go next to it; files of the same
names are replaced. "Normal" quality suits chiptunes; "Best" makes bigger
files. The files play the song as FamiTracker does, with the mixer's faders,
pans, Soft and Space (View > Mixer). Every channel is in the files, also the ones muted in the pattern
editor; they're muted again afterwards.

A track whose notes click on the 2A03's pulses (see "Clicks in held notes
on the 2A03's pulses") is exported as it is, and the reply says so:
`polish` with the touch `clicks` nudges those notes, then export again.

## Safety

- FamiTracker only listens on 127.0.0.1, so other computers can't reach it,
  unless you allow other computers on your network (then only home networks
  and Tailscale; see [Over the network, safely](#over-the-network-safely)).
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
- **Another computer can't connect** (it times out, or
  `famitracker-mcp-linux` says "didn't answer"): Windows' firewall keeps
  FamiTracker out. In **Windows Security > Firewall & network protection >
  Allow an app through firewall**, tick FamiTracker for Private networks
  (and Public, if Windows thinks your home network is public; or set it to
  Private in its network settings). Check the address too: both computers
  on the same network, or both in your Tailscale.
- **"Connection refused" from another computer**: **Allow connections from
  other computers on my network** isn't ticked, the connection is off, or
  the port isn't the one in the address.
- **"FamiTracker only answers computers on a home network"**: the other
  computer's address isn't a home network's or Tailscale's, as through some
  VPNs. Connect through the home network or Tailscale.
- **"FamiTracker only answers requests to this computer's addresses or
  name"**: the client used a name FamiTracker doesn't know as its own (a
  name of your own in a hosts file, say). Use the address from the dialog.

## Not yet

- Recording ideas from a phone page into the clips.

## For developers

- `Source/Api/` is plain C++14 without MFC, built and tested on Linux:
  `Json` (reader and writer), `Http` (request framing), `Mcp` (the protocol:
  per-request metadata of 2026-07-28 and the initialize handshake of
  2025-03-26 to 2025-11-25, Host, Origin and token checks), `Music` (cells,
  note names, bars and beats, keys and modes, writing a part into a range of
  rows, and following the song through its jumps, skips and halts),
  `Presets` (also the VRC7's patches, the 5B's own sounds and the presets
  as the 5B plays them, and `DesignPreset`: a sound described in words as
  sequences), `Wavetables` (the N163's wave sounds), `FdsSounds`
  (the FDS's sounds: their waves, modulator tables and sequences), `Chords` (chords read from names and degrees,
  progressions laid out on bars, colors, and the chords heard in notes),
  `Sections` (a track's named runs of frames, kept with their frames as
  frames go in, come out, move or are copied, and the `BF SECTIONS` block,
  read with every read checked; the frame editor, the piano roll's ruler
  and `CFrameAction` use it too), `ChannelMix` (the mixer window's faders,
  starting pans, Soft and Space by channel ID, the space they send to, and
  the `BF MIXER` block, version 2 only when Soft, Space or the space is
  used, read the same way),
  `Arrange` (chord parts in styles, bass lines, drum grooves with fills, a
  harmony voice, a counter-melody), `Picture` and `Png` (the piano-roll
  picture, a palette PNG with its own small deflate), `Jingles` (the
  jingles' scores, in semitones from the key note and sixteenth notes),
  `SoundEffects` (the sound effect kinds, built a tick at a time or from
  layers, and `DesignSoundEffect`, which builds sounds described in words
  from layers: slides, envelopes, repeats, gusts, flutters, crackle and
  drops), and
  `Tools` (the tools, against the `SongHost` interface in `Song.h`, the
  server's instructions and the recipes served as MCP prompts), and
  `ProblemList` (`check_song`'s JSON read back for FamiTracker's problems
  panel, and the marks the pattern editor and the piano roll draw). Tool
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
  HTTP requests and hands each to the UI thread with `SendMessageTimeout`,
  each with its client's address (`HttpRequest::Peer`). With other computers
  allowed it binds every IPv4 address and closes connections from addresses
  outside home networks at once; `ListNetworkAddresses` gives the dialog this
  computer's addresses (`GetAdaptersAddresses`).
- `Source/Api/Network.cpp`: who may connect from the network (`MayConnect`:
  loopback, 10/8, 172.16/12, 192.168/16, 169.254/16, 100.64/10, or one of
  this computer's own addresses), the Host headers that name this computer
  (`NamesThisComputer`), the names files may have in the Inbox and the file
  route (`IsPlainFileName`, `IsInboxFile`, `IsExportFile`), links and base64.
  `HandleMcp` routes `/mcp`, `/mcp/core` (the tools of `ToolSet::CoreTools`
  with `ToolSet::CoreInstructions`, no prompts), `GET /files/<name>` and
  `PUT /inbox/<name>`; `ToolSet::Call` takes a `Caller` whose `FilesUrl`
  makes `export_for_game` link its files.
- `Source/Api/Bridge.cpp` and `Source/Api/tools/famitracker-mcp.cpp`: the
  connection for Claude Code in WSL, an MCP server over stdio. Each JSON-RPC
  line becomes the HTTP request Claude Code would send (with the protocol
  headers the endpoint checks) and each answer a line; an answer refused
  before it was read (a wrong access code) still goes to its request.
  `BridgeSocket.cpp` sends it to 127.0.0.1 (Winsock, or BSD sockets for the
  tests). On Windows it reads the port and the access code from
  `HKEY_CURRENT_USER\Software\FamiTracker\Claude Code` for every message;
  `--port` and `--token` override them. With `--host` (an address or a
  name) it connects to FamiTracker on another computer instead, within 10
  seconds, and `--core` picks `/mcp/core`. Its sources build alone as the
  `ftbridge` library, and CI links it statically against musl on Alpine for
  the release's `famitracker-mcp-linux`. The tests also run it against the
  endpoint over real sockets, by address and by name.
- `Source/McpConnection.cpp`: `SongHost` over the open document, the
  message-only window that answers on the UI thread, the settings (registry
  section "Claude Code") and the dialog. Song changes go through
  `WriteSongParts` in `InsertRecording.cpp`, one undoable action each, which
  can first put whole frames into the frame list or take them out
  (`insert_bars`, `remove_bars`); Undo puts the frame list back. Paths
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
  modulation from silence, never stepping past the counter's ends (it holds
  there instead) and back at its start level at the end, so a sample started
  from another level (`Zxx`) walks the same way and ends where it started;
  `SongHost::AddDpcmKit` adds the samples and the instrument that maps them
  to keys, and `RefreshDpcmKit` brings a kit an older version made up to
  date.
- `Source/Api/References.cpp` and `StyleGuide.cpp`: the reference library
  (references as indented JSON, searching them, a sound as a preset) and
  the built-in style guide. The library's files are read and written
  through `SongHost::ListLibrary`, `ReadLibrary`, `SaveLibrary` and
  `DeleteLibrary`; `import_nsf` opens its module with `SongHost::OpenText`,
  which `ImportNsfDlg.cpp` (File > Import NSF) shares. `study_module` reads
  a tracker module with `Source/Nsf/Tracker.cpp` (MOD, S3M, XM and IT
  patterns, followed through their breaks and jumps into a module of one
  track with a channel layout of its own), or a MIDI file with
  `Source/Nsf/MidiFile.cpp` (read into the same kind of song, with its
  meter and channel names), or a FamiTracker module with
  `Source/Nsf/FtmFile.cpp` (a track at a time), and studies it with
  `Study.cpp`.
- `Source/ChipSwitch.cpp`: what `set_expansion` and Module > Module
  properties do when the chips change: the module's channels for a set of
  chips, counting the notes on the channels that go, emptying them in every
  track, moving their `Fxx`, `Bxx`, `Dxx` and `Cxx` to the last channel (a
  free effect column on the row; a frame whose pattern other frames play
  differently gets its own), and `SelectExpansionChip` moving the channels
  that stay to their new columns. `Source/Tests` checks it against the
  stand-in document.

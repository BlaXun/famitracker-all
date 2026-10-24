# Roadmap: beginner-friendly FamiTracker

Branch `beginner-friendly`. The goal is a FamiTracker that someone with no
music training can use to write VRC6 chiptune music for a GameMaker game,
which plays the tracks as OGG files.

## Principles

- **Keep songs readable everywhere.** The piano roll is another view of the
  same pattern data, profiles live in program settings, and presets are
  ordinary `.fti` files. Songs must keep opening in vanilla FamiTracker,
  0CC/Dn-FamiTracker and FamiStudio. The additions to the `.ftm` file are
  blocks of their own that other versions skip: the recorder's clips
  (`BF CLIPS`; there, linked copies are ordinary notes) and the tracks'
  named sections (`BF SECTIONS`).
- **Stay in the UI and export layers.** The NSF driver is binary-only in this
  repo (see CLAUDE.md), so no feature may need new driver commands.
- **Full FamiTracker stays one click away** through a "Show everything" option.

## Where things stand

Everything built so far is in the `beginner-friendly` build, the "Latest"
download (`build-beginner-friendly`): the foundation, recording, clips, the
piano roll, the export for games, sound effects and the Claude Code
connection. Most of it was built on the `recorder` branch (see Feature
branches) and merged in October 2026.

The steps that make FamiTracker itself simpler followed in October 2026,
through the `next` branch (its build is the `build-next` pre-release, and
`beginner-friendly` moves up to it once it's been tried): the instrument
library and the drum grid (3), profiles (4), tutorials (6), and the clips
panel, which keeps the song's clips in sight beside the pattern editor.
What's left of the plan is listed under each step.

## 1. Foundation

- [x] `FamiTracker.vcxproj`/`.sln` for current Visual Studio. The toolset
  follows the installed Visual Studio; MFC stays static with the MBCS
  character set.
- [x] Boost replaced with `std::unique_ptr`. DirectSound now comes from the
  Windows SDK instead of the DirectX SDK.
- [x] GitHub Actions workflow (`.github/workflows/build.yml`) that builds Win32
  Release and Debug and uploads `FamiTracker.exe`.
- [x] Green Release and Debug builds on CI, with a "Build report" check run
  listing all diagnostics. The exe links MFC and the CRT statically and
  depends only on Windows system DLLs.
- [x] Warning-free at `/W3`. The fixes: driver table pragmas scoped,
  `GetVersionEx` replaced, and a signed menu-count loop.
- [x] Rolling GitHub releases: a green build of `beginner-friendly`,
  `recorder` or `api` replaces that branch's `build-<branch>` release.
- [ ] x64 build. The project has x64 configurations, but CI builds Win32
  only. Low priority: the 32-bit build runs on 64-bit Windows.

## 2. OGG export with loop points

Done (see `MCP.md`): **File > Export for games (OGG)** saves the selected
track, or all of them, as OGG files next to the song.

- FamiTracker renders the song past its second time through the loop and
  notes the sample where every row starts (`CSoundGen::GetRenderMarks`).
- The loop is found from the song's jumps, skips and halts (`Bxx`, `Dxx`,
  `Cxx`). The file holds the intro and the loop once, or the loop twice when
  the first pass starts differently (a note or effect from the intro hanging
  over). The loop points move together by up to 2 ms to where the waves
  meet, so the jump doesn't click.
- The dialog and Claude Code give the GameMaker code
  (`audio_sound_loop_start` and `audio_sound_loop_end`, GameMaker 2023.2 and
  later), and the files carry `LOOPSTART`/`LOOPLENGTH` comments for other
  engines. A song that ends with a halt (`Cxx`) plays once, like a jingle.

## 3. Sounds before settings

Done:

- **Instrument > Instrument library** (Ctrl+Shift+L, also under the
  new-instrument button; `Source/InstrumentLibrary.cpp`): the sound presets
  of `Source/Api/Presets.cpp` (13 for the 2A03, MMC5 and VRC6, plus the
  VRC7's 15 built-in sounds) for the kinds of channel the song has, with what
  each is for. Clicking a sound plays it with FamiTracker's own engine: it
  goes into the song as an instrument while it's tried, one at a time,
  playing a short phrase (or a beat, for drums), and comes out again unless
  Add keeps it.
- **Edit > Drum grid** (Ctrl+Shift+D; `Source/DrumGridDlg.cpp`, with the
  arithmetic in `Source/DrumGrid.cpp`, tested on Linux): the noise channel a
  bar at a time, a line each for a kick, a snare, a hi-hat and an open
  hi-hat and a column for each row. Clicking a step puts a drum on or takes
  it off and plays it; dragging along a line fills the empty steps. Ready-made
  beats fill a bar, which can be copied to the next bar or to every bar. Bars
  and beats follow the row highlights, the step playing lights up (and in
  follow mode the grid turns to the bar playing), each change is one Ctrl+Z
  (which works inside the grid too), and the drums' instruments are made
  from the presets the first time they're needed.

## 4. Profiles

Done (`Source/Profile.cpp`), and opt-in: the first start asks once, and
View > Profile changes it later. A profile says how much of FamiTracker
shows; the songs themselves stay ordinary modules.

- "VRC6 game music" makes three changes, each of which can be switched off
  in View > Profile: new songs start with the VRC6 chip, 120 BPM and eight
  instruments from the presets (a lead, chords, a bass, a kick, a snare, a
  hi-hat, a pad and a sawtooth bass); BPM shows in place of speed and tempo
  in the song settings (typing or stepping a BPM picks the speed and tempo,
  as Claude Code does); and rarely used menu items are hidden (text import
  and export, the masks, expand and shrink, the register and DPCM displays,
  NTSC and PAL, the engine speed and a few more; their shortcuts still
  work).
- "Everything" is FamiTracker as it was, and View > Show everything switches
  between the two.

Not done from the plan: hiding the DPCM channel (the pattern editor has no
way to hide a channel) and choosing the starter instruments.

## 5. Piano roll

Done (see `RECORDER.md`): a horizontal view under the pattern editor, time
left to right and pitch bottom to top, that follows playback. It shows every
channel's notes with the edited one bright, and draws, paints a melody, moves,
copies, resizes and erases notes, keeps new notes in a key, places clips with
the mouse, and puts effects on notes (vibrato, tremolo, chords as arpeggios,
slides, glides, bends and fades). Every change is one Ctrl+Z.

Still open from the plan:

- A chord helper that spreads a chord across free channels (a chord as a
  `0xy` arpeggio on one note is done).
- Mood starters that lay down chords, bass and drums for moods like "cozy
  town" or "boss fight" (see "start from a groove" under the recorder).

## 6. Tutorials

Done: Help > Tutorials (`Source/Tutorial.cpp`) shows FamiTracker a step at a
time. The window dims except for the part a step is about, which gets a
yellow frame, and a note beside it says what to do. The step goes on when
the user has done it (switched editing on, typed notes, played, stopped,
added an instrument or a clip, saved, used a menu item or button) or
presses Next, and the frame turns green for a moment first. The dimming lets
the mouse through and the note never takes the keyboard, so FamiTracker
works as usual meanwhile.

Tutorials are text files of steps (the format is in
`Source/TutorialScript.h`, read by plain C++ that `Source/Tests` checks,
built-in tutorials included). Four come with FamiTracker, from
`res/tutorials`: your first tune, sounds and drums, clips and the piano
roll, and music for your game. More can go in a `Tutorials` folder next to
FamiTracker.exe, with no rebuild.

Still open: annotated example songs, since module comments can already pop
up when a file is opened.

## 7. Problems panel

Done (October 2026, on `next`): **View > Problems** shows what Claude
Code's `check_song` finds, for people working by hand, in a panel docked
along the bottom of the main window (right-click it to dock it on the right
instead; drag its edge to make it bigger). It comes back when FamiTracker
starts if it was open.

- Notes that vanish, notes that click, clashes, notes out of a channel's
  range, silent and busy bars and notes outside the key, grouped by kind
  with how many of each, the ones heard worst first. A click on a kind
  folds or unfolds it.
- It checks the selected track again a moment after each change, and after
  an instrument's envelope, the speed or the track changed, but not while
  the song plays. **Check** (F5 in the panel) checks at once; "Check as you
  edit" in its right-click menu switches the checks off.
- Double-click a finding, or select it and press Enter, to go there: the
  pattern editor's cursor goes to its frame, row and channel, and the piano
  roll scrolls to it.
- The pattern editor marks each finding with a tick along the channel's
  left edge on the note's row, the piano roll with a dot on the note: red
  for what's heard worst (notes that vanish, clicks, clashes), amber for
  notes out of range, grey blue for the rest. The finding selected in the
  panel gets a tick as tall as the row and a ring around its notes.
- Each finding has a button. **Nudge**, on a note that clicks, moves it a
  hair sharper or flatter so its vibrato stays clear of the period where
  the pulse restarts its wave, as `polish` with the touch `clicks` does on
  that note's bar: one Ctrl+Z, and the count goes down. **Show**, on a note
  that vanishes, selects that sound's notes in the piano roll, where its
  Instrument button gives them a sound that lasts. **Explain** says what's
  wrong with the rest (a clash, a note outside the key) and what helps.
  Nothing changes without a click.

The panel's keys: the arrows, Page Up and Down, Home and End to move, Enter
to go there, Space for the button, Left and Right to fold, Ctrl+C to copy
the message, Esc back to the pattern editor.

## 8. Song sections

Done (October 2026, on `feat-sections`): a track's frames can be named
(Intro, A, B, Bridge, Outro), so a song's form is something to see and to
talk about.

- Select frames in the frame editor and right-click > **Name section**.
  Each section is a coloured band beside its frames, named at its first
  frame, with where the song loops back to; the column appears once the
  module has a section. A double-click on a band renames it.
- Right-click a section to rename it, take its name off, select its
  frames, move it up or down past the section beside it (its patterns
  with it) or duplicate it after itself (the copy plays the same
  patterns). Each is one Ctrl+Z.
- Frames inserted, removed, moved, dragged or duplicated keep their
  sections: frames put in inside a section join it, removed ones leave it.
- The piano roll's ruler names each section at its first bar.
- Claude Code names the sections as it builds a song (`name_section`),
  lists them in `get_song`, and its tools take a section's name in place
  of bars (see `MCP.md`).
- They're saved in a block of their own (`BF SECTIONS`), which other
  versions skip.

## 9. Stereo

Done (October 2026, on `feat-stereo`): FamiTracker plays in stereo, which
the NES never did, so each note can play from the left, the middle or the
right.

- The pan effect `8xx` places a channel between the speakers from its row
  on: `800` all the way left, `880` the middle, `8FF` all the way right
  (`840` halfway left). It lasts until the next `8xx`, and every channel
  starts in the middle, so a song without it sounds and saves exactly as
  before. Type `8` in an effect column; Help > Effect list describes it.
- The piano roll shows a panned note with an arrow at the end it leans to,
  says where in its hover text, and its Effects menu (double-click a note,
  or E) has **Pan (stereo)**: Left, Halfway left, Center, Halfway right and
  Right for the selected notes, one Ctrl+Z. The notes after keep their pan,
  and moving, copying or pasting notes keeps each where it played.
- Every chip pans: the 2A03, VRC6, VRC7, FDS, MMC5, N163 and 5B. Playback,
  File > Create WAV and File > Export for games are stereo; the OGG files'
  loudness is measured over both channels.
- Claude Code places notes, parts and echoes and sets a stereo picture with
  `mix` (see `MCP.md`, Stereo).
- NSF files and the NES are mono: NSF export leaves `8xx` out. FamiTracker
  0.4.6 opens such a song, shows "(invalid)" on rows with `8xx` in their
  first effect column and plays it in the middle. 0CC-FamiTracker and
  Dn-FamiTracker number effects of their own after 0.4.6's, so they may
  read `8xx` as one of theirs: an open question below.

## Feature branches

Larger ideas get their own branch made from `beginner-friendly` and merge back
when ready.

- **`recorder` (merged into `beginner-friendly` in October 2026):** whistle,
  hum or tap and get notes whose length matches the sound, recorded in
  FamiTracker (a toolbar button, with a count-in and click at the song's
  tempo) or from a WAV file. See `RECORDER.md`. Done:
  - Expression: loudness, slides into notes and vibrato from the
    performance, and a richer lead sound. Sung syllables ("la la la") become
    notes of their own.
  - Harmony backing: find the key, pick chords that fit the melody bar by
    bar, and write a bass line and arpeggiated chords.
  - Clips: takes and pattern selections kept with the song, placed as copies
    or linked, with a clip editor.
  - The piano roll (step 5) and the export for games (step 2).
  - The Claude Code connection, merged from `api` (below).
  - Sound effects for the game, each a track that plays once and exports
    like the music: Edit > Sound effect from your voice ("pew", "boing",
    "kssh" into the microphone; the take stops by itself and plays back as
    the NES would), and Claude Code's `make_sound_effect` (34 kinds with
    variants, ambience that loops, sounds described in words, or a
    recording).

  Still to do, in this order, on a branch made from `beginner-friendly`
  again:
  1. More natural input: start from a groove (pick a mood, tempo and key,
     get a few bars of drums, bass and chords, and sing the melody over
     them); re-sing just a selection (the song plays a bar before, the take
     replaces only those rows); tap the tempo instead of setting speed and
     tempo.
  2. Chords by name ("C G Am F") or strummed on a guitar.
  3. A small page on the phone that records ideas over Wi-Fi into the clips.
- **`api` (merged into `recorder`, and with it into `beginner-friendly`;
  see `MCP.md`):**
  FamiTracker is the MCP server itself, speaking Streamable HTTP on
  `127.0.0.1` with an access token, opt-in in File > Connect Claude Code.
  Claude Code in WSL connects through `famitracker-mcp.exe`. Claude works in
  musical terms (bars, beats, note names, instruments, keys and chords), can't
  hear, so it gets checks and a picture of the notes, and its ideas arrive as
  clips unless the user lets it change the song directly, always undoable.
  Its 26 tools:
  - read and write parts, presets, transpose and change mode, chords and a
    bass line, echo, vibrato, moods ("darker", "happier", "underwater") in
    one go, checks, a picture of the notes, play;
  - versions of a song as new tracks (night, tense for battles, and others)
    and jingles (level up, victory, item, new quest, discovery, rest, defeat,
    danger), on any track of the module;
  - checking and setting the loop, and the export for games;
  - switching the expansion chip (it asks first when notes would be
    cleared), with the VRC7's built-in sounds by name;
  - speed and tempo from a BPM (rows of even length first) or as
    FamiTracker's own numbers;
  - sound effects for the game, from 34 kinds, described in words or from a
    voice recording; ambience loops.

  In October 2026, on `next`, arrangements with depth, because tracks often
  came out plain: chords by name or degree and in styles (held or rhythmic
  arpeggios, stabs, broken chords, guide tones) with sevenths or richer
  colors, bass lines in styles (walking, octaves, syncopated...), drums with
  grooves and fills, a harmony voice, a counter-melody, finishing touches,
  grooves to start from by mood, repeating and varying sections, a review
  that scores the arrangement's depth, instruments described in words with
  livelier presets, soundtrack notes kept beside the song, instructions that
  teach the craft, and recipes served as MCP prompts. 36 tools now.

  Night versions that sound like night (asked for when one only came out
  slower): `make_version` "night" lowers the parts that sit high, thins fast
  runs to their outline, holds the notes into each other with even
  dynamics, turns struck chords into a held pad (sevenths if asked), gives
  the bass long notes, fades the drums to a soft pulse, and uses a swelling
  lead, a slower and wider vibrato and a longer, softer echo, saying what
  it changed.

  Sound effects described in words (asked for: "a bird flying away", "a
  heavy wind"): `make_sound_effect` builds them from layers on the 2A03's
  channels (slides, hits and swells, repeats, gusts, flutters, crackle and
  drops), and has nine more kinds from nature and machines: wind, rain,
  fire, thunder, birds, wings, an engine, an alarm and a heartbeat. Ambience
  loops seamlessly in the export for games.

  FamiTracker 0.4.6's effects (asked: does Claude know them all?): the
  `effects` tool explains each one and the classic techniques made with
  them, and `write_notes` refuses effects 0.4.6 doesn't have (the `Lxx` and
  `Oxx` of later versions) or that the channel can't play, saying what to
  use instead.

  The song's form, as the user works mostly through Claude Code:
  `insert_bars` and `remove_bars` put empty bars in or take bars out
  anywhere, in every channel, and the loop's jump and the song's end move
  with the music. Whole frames go into or out of FamiTracker's frame list,
  as Insert frame does, so the frames after keep their patterns and linked
  clips.

  Listening help: `play` loops bars until stopped, and plays only some
  channels or leaves some out, giving the user's own mutes back when it
  stops.

  Versions to compare: `keep_version` keeps the track as it is now as a
  track of its own before Claude tries something, and `compare_versions`
  plays the same bars of each, one after the other, for the user to pick.

  N163 instruments (asked for: something more complex than the VRC6):
  twelve wave sounds drawn for the N163's up to 8 channels, whose timbre
  moves as a note plays (a rotating organ, shimmering strings and choir,
  brass opening up, an electric piano and a bell that mellow, a sweeping
  pulse lead, a growling bass), each wave placed in the chip's shared memory
  where no other instrument's is.

  Ideas not built yet: FDS instruments, and recording through Claude.
- **NSF import and a reference library (October 2026, on `next`):** NSF
  files open as readable FamiTracker songs, and Claude learns from them.
  - Our own portable player core (a 6502, the NSF's bank switching, the 2A03
    and the VRC6, VRC7, FDS, MMC5 and N163 registers), tested on Linux,
    rather than the archived `nsf-importer` fork (NSF Importer 0.1 on
    0.3.6), which writes every frame as a row: faithful but unreadable.
  - From the registers, music: the rows and speed found from when notes
    start (fractional tempos too), notes on rows, legato steps and chord
    changes, fast chord flickers as `0xy`, duties as `Vxx`, instruments
    rebuilt from each note's volume, duty and pitch shapes (blooming attacks
    kept), DPCM drums with their samples, the loop found even when a vibrato
    runs free, and track names from an `.m3u` playlist. Measured against
    FamiTracker's own playback of the result: most tracks 95-100% the same
    sound and pitch.
  - File > Import NSF picks out the music from the jingles and sound
    effects; `import_nsf` does it for Claude; `nsf2ftm` from a command line.
  - Studies: what makes a track work, in words a composer uses (form,
    harmony, each channel's role and rhythm grid, bass figures, drum grooves,
    tricks between channels, the sounds' articulations), kept in a
    reference library next to FamiTracker.exe for every session
    (`study_nsf`, `references`, `reference_sound`), without the melodies;
    and a built-in guide to the classic NES styles from studies of Konami,
    Capcom and Nintendo soundtracks. `design_instrument` learned the
    articulations those soundtracks are made of.

  A DPCM drum kit of our own (a kick, a snare and two toms, synthesized and
  encoded here) plays on the DPCM channel by name (`add_sample_drums`).

  Music modules as references (asked for in October 2026): `study_module`
  studies a MOD, S3M, XM or IT file from its patterns alone and keeps it in
  the same library, so Claude can write NES music in its style ("similar,
  but not a copy": the tunes aren't kept, and its samples aren't read).
  Drums are told by their instruments' names or by playing one note; an
  instrument with a sample a key splits into its drums; the song is
  followed through its breaks and jumps.

  MIDI files as references (asked for on 21 October 2026): `study_module`
  reads `.mid`, `.midi` and `.rmi` files too (`Source/Nsf/MidiFile.cpp`).
  The notes go on a grid of 4, 6, 8 or 12 rows a beat, whichever they start
  on; each part spreads over channels of a note at a time, the highest note
  on the first so a melody stays whole; the drums go on a channel for each
  kind, named after General MIDI's drums. The study counts bars in the
  file's meter (3/4, and 6/8 as two beats of three eighths), takes the tempo
  the file keeps longest, loops where RPG Maker's controller 111, EMIDI or
  `loopStart` say, and leaves out grace notes. A track named "Melody",
  "Lead" or "Soprano" is the lead.

  The user's files next to FamiTracker.exe (asked for on 21 October 2026):
  the reference library (`References`) and the recordings of Record from
  microphone and Sound effect from your voice (`Recordings`) are folders
  next to FamiTracker.exe, so they move with it (`Source/AppFolders.cpp`).
  The old folders in Documents move there by themselves, to another drive
  too; where FamiTracker's folder can't be written, they stay in Documents.
  `make_sound_effect` takes the recording `"latest"` for the newest take.

  Depth and atmosphere (from 21 October 2026 the aim for the music itself:
  the warm, lush and nostalgic sound of modern FamiTracker music, which
  gets its depth from technique rather than from an expansion chip).
  Studies of modern FamiTracker modules found how it's made, and the tools
  now do it: `lush` chords (an instrument for each chord shape whose
  arpeggio loops through four or more of its notes, two frames each, so
  sevenths and ninths ring; a `nostalgic` groove plays them), echoes that
  follow the lead at about 40% and can be detuned a step, `set_swing`
  (the speed alternating every half beat, as 0.4.6 composers swing
  without grooves), `mix` (a soft mix by role, and the DPCM level that
  softens the triangle and the noise the way the NES mixes them), six
  VRC7 custom sounds of our own design (electric pianos, a bell, a pad, a
  bass, a mallet), the technique reference and an `atmospheric` section in
  the style guide. FamiTracker reports instruments' looping arpeggios, so
  the review, the chord fitting and the echo hear those chords. Still to
  come: studying `.ftm` modules for the reference library, and moving lush
  chords into another mode (their instruments keep their shapes for now).

## Open questions

- Some recordings "don't sound correct" (maybe background noise). Waiting
  for a sample take from the `Recordings` folder next to FamiTracker.exe
  (`Documents\FamiTracker recordings` before 21 October 2026) to diagnose.
- Stereo in other trackers: the pan is effect 33 in the pattern data, the
  next number after 0.4.6's effects. 0CC-FamiTracker and Dn-FamiTracker
  numbered their own added effects from there, so a song with pans may
  play there with one of their effects in those cells. Not checked yet. If
  it does, the pans could move to a block of their own (like `BF CLIPS`),
  which other versions skip, with the pattern editor still showing them.

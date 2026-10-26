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
- The dialog's Loudness (remembered): each track's loudest peak at -1 dBFS,
  as before, or every track brought to one loudness (-22 LUFS for calm
  places, -20 for most tracks, -19 for action and bosses, or a number), so
  the game's tracks sound equally loud; a peak that would pass -1 dBFS holds
  a track back, and the result says so.

## 3. Sounds before settings

Done:

- **Instrument > Sound browser** (Ctrl+Shift+L, also under the
  new-instrument button; `Source/SoundBrowserDlg.cpp`, which grew out of the
  instrument library): one list of every sound the song can use, for the
  chips it has on: the ready-made sounds (the presets of
  `Source/Api/Presets.cpp` for the 2A03, MMC5, VRC6 and 5B, the 5B's own, the
  VRC7's 15 built-in sounds and custom patches, the N163's wave sounds and the
  FDS's sounds), each sound of your reference library's studies (as
  `reference_sound` makes it), and the song's own instruments. Find takes
  words (bell, warm, pluck, bass, a game or a reference's title); filters
  pick a kind (leads, basses, pads and chords, plucks, bells, keys, drums,
  effects), where sounds come from and the channel. Each sound shows its
  words, where it's from and the channel it plays on, and warns when its
  notes are gone within four frames (a tune's notes on it sound missing) or
  when its vibrato clicks on held notes of a 2A03 pulse. Clicking a sound
  plays a short phrase (or a beat, for drums) on its channel with
  FamiTracker's own engine, and while the list is picked the note keys (or a
  MIDI keyboard) play it: a sound not in the song goes in as an instrument
  while it's tried, one at a time, and comes out again unless Add to the
  song keeps it (named, and picked in the instrument list). Claude's
  `find_sound` searches the same list.
- **The sound browser in groups, and listening by clicking** (October 2026,
  `feat-sound-groups`): the kinds sit on the left with their counts (all
  sounds, leads, basses, pads and chords, plucks, bells, keys, drums,
  effects), and the list has a heading for each kind and, inside it, for
  the ready-made sounds, each game of the reference library (A to Z) and
  the song's own instruments. A click on a heading folds it or opens it
  again; a kind with more than 40 sounds from the library starts with its
  games folded, so the ready-made sounds and every game's name show at
  once, and words in Find open everything they find (and show every kind
  when the kind picked has none of it). Every click on a
  sound plays it, the one already picked too, and each row has a play mark
  that turns green while it plays; the arrow keys play each sound they
  reach (held down, the one they stop on), Space plays it again. The
  phrase suits the kind: a short tune for a lead, a bass figure for a
  bass, two held notes for a pad, a broken chord for a pluck or keys,
  ringing notes for a bell, one long note for an effect, a beat for drums
  in each drum's own rhythm (the song's own drums at the pitches it plays
  them). Starting another sound or closing the browser stops the one
  playing. The window can be resized (the filters go to two rows when
  it's narrow) and keeps its place, kind and folds while FamiTracker runs.
- **A sound library, twelve soundtracks' sounds and the other chips'**
  (October 2026, `feat-sound-library`): the browser listed 32 sounds for a
  new 2A03 song; it lists 975 now. FamiTracker's own sound library
  (`Source/Api/SoundLibrary.cpp`, with the N163's, FDS's and DPCM's in
  `Wavetables.cpp`, `FdsSounds.cpp` and `DpcmKit.cpp`) adds 234 sounds of
  distinct characters in every kind: 156 for the 2A03's pulses, triangle
  and noise, the VRC6's pulses and sawtooth and the 5B, 21 VRC7 custom
  patches, 30 N163 and 25 FDS sounds and two more kits of samples. Each is
  heard for more than a few frames unless it's meant to be short (and says
  so), none of the 2A03's has a vibrato that clicks, and no two listed
  together sound alike (the tests measure every pair's envelope, restrikes,
  brightness, pitch and a wave's harmonics). The studied sounds of twelve
  soundtracks come with FamiTracker (`Source/Api/GameSounds.cpp`: 615
  sounds of 152 tracks of Castlevania, Castlevania 3, Dr. Mario, Final
  Fantasy 3, Journey to Silius, Nintendo World Cup, Partena no Kagami,
  Punch-Out!!, Robot Block, Strider Hiryu, Super C and The Legend of
  Zelda), game by game, each sound once with the tracks it plays in, and
  once when the reference library has it too. The ready-made sounds of the
  chips the song doesn't have come last in each kind, folded under "FDS
  (not in the song yet)" and the like; Add to the song asks, then adds the
  chip as Module properties does (every note stays) and the sound. With the
  355-study library the list takes the same time to make as before (the
  games' sounds in it are listed once).
- **The browser's fixes** (October 2026, `feat-fixes-browser`): Enter in
  Find picks the sound that matches best, wherever it is in the list (the
  song's own "Choir pad" after the ready-made pads), opens its heading if
  it was folded, plays it and says it's the best match; the kinds on the
  left leave out a kind with no sounds ("Other (0)"). Every sound plays at
  about the level of a 2A03 pulse at 15, from a click, the note keys or
  Listen (a VRC7 sound at volume 13, an FDS sound at 8), so browsing
  doesn't jump in loudness (a channel's fader in the mixer still counts).
  After File > Create WAV or an export for the game, the 2A03's
  sounds are heard at once (they stayed silent until a song played, as in
  0.4.6). A studied sound's name starts with its track's title before a
  parenthesis ("Vampire Killer lead", "Beginning bass", where it was
  "Killer (Courtyard) lead"), and names that come out alike in two studies
  are numbered. Adding a sound's chip says plainly that it clears the undo
  history.
- **Listen** (the speaker button at the end of the instrument list's
  toolbar, and in the Instrument menus): the instrument picked plays its
  kind's phrase on the channel the song plays it most, as the sound
  browser plays it, without opening its editor. A plain click in the list
  still only picks the instrument for writing notes.
- **View > Drum grid** (also in the Edit menu, Ctrl+Shift+D;
  `Source/DrumGridPanel.cpp`, with the arithmetic in `Source/DrumGrid.cpp`,
  tested on Linux): the noise channel a bar at a time, a line each for a
  kick, a snare, a hi-hat and an open hi-hat and a column for each row, in a
  panel docked under the piano roll (right-click it to dock it on the right).
  Clicking a step puts a drum on or takes it off and plays it; dragging along
  a line fills the empty steps. Ready-made beats fill a bar, which can be
  copied to the next bar or to every bar. It shows the bar the cursor is in
  and goes with it (in follow mode, with the song as it plays); < and > show
  another bar until the cursor moves. Bars and beats follow the row
  highlights, the step playing lights up, each change is one Ctrl+Z, and the
  drums' instruments are made from the presets the first time they're
  needed.

## 4. Profiles

Done (`Source/Profile.cpp`), and opt-in: the first start asks once, and
View > Profile changes it later. A profile says how much of FamiTracker
shows; the songs themselves stay ordinary modules.

- "VRC6 game music" makes three changes, each of which can be switched off
  in View > Profile: new songs start with the VRC6 chip, 120 BPM and eight
  instruments from the presets (a lead, chords, a bass, a kick, a snare, a
  hi-hat, a pad and a sawtooth bass); BPM shows in place of speed and tempo
  in the song settings (typing or stepping a BPM picks the speed and tempo,
  as Claude Code does; a song at speed 1, a sound effect's, shows as "Speed
  1 frame", and typing 1 or stepping past 400 BPM sets it, and the status
  bar says "1 frame a row" for it where it said 900 BPM); and rarely used
  menu items are hidden (text import
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
copies, resizes and erases notes, places clips with the mouse, and puts
effects on notes (vibrato, tremolo, chords as arpeggios, slides, glides, bends
and fades). Every change is one Ctrl+Z.

It shows the key and the chords, so you see which notes fit: the lanes of the
key's notes are a little lighter, the key note's warmer, and a strip under the
bar numbers names the chord of each bar (or half bar, where it changes) as
the parts play it. The Key button finds the key from the notes (Auto) or
takes one you choose: a key note and major, minor, their pentatonic five
notes, dorian, mixolydian, lydian, phrygian, harmonic minor or locrian. A
chosen key is saved with the track, and Claude Code's tools use it too. With
Snap to key on, notes you draw or move land on the key's notes (hold Alt for
any note). Point at a chord in the strip to see its notes lit.

The Chords button (October 2026, on `feat-chord-helper`) spreads a chord
across the channels that play nothing in its bars, a held note on each,
voiced together under the melody and over the bass. Pick one of the key's
chords, the ones the strip names there, or type a chord ("Am7", "C/E") or a
chord a bar ("C G Am F"); it goes on the bar at the cursor or over a
selection. Each voice gets a quiet sound for its chip (on the VRC6 Plus's
soft layer a string pad of its own, since two N163 channels can't share
one). When fewer channels are free than the chord has notes, it says so and
offers fewer voices or the chord as an arpeggio on one channel; Again puts
the same chord on the next bar. One Ctrl+Z takes it back. A voice held to
where the song goes back to its loop stops on the last row that plays
(released, where its sound fades), also when a jump (Bxx) goes back before
the last pattern's end, so it doesn't ring on into the loop.

Mood starters (October 2026, on `feat-piano-polish`): the Groove button
lays down drums, a bass line and chords for a mood, the moods of Start from
a groove (town, overworld, battle, boss, dungeon, calm and eight more),
under the selected bars or for 4, 8 or 16 bars from the cursor's bar, on the
channels the melody doesn't use. An empty song shows "Start from a groove"
in the middle of the piano roll and takes the mood's tempo; a song with
notes keeps its own. More choices opens the groove dialog for the tempo and
the key. One Ctrl+Z takes it all back. On a narrow window the piano roll's
buttons now go on two rows (or more) instead of dropping off the end, and
a piano roll too short for that keeps the rest under More.

With that, everything planned for the piano roll is in.

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

Done too (October 2026): **Help > Example songs**, five short songs written
for the purpose, each opening as a new, untitled song (saving asks for a
name, so the example stays as it is) with its comments and a walkthrough:
a first song on the 2A03 alone (a tune, a bass, drums, and what an
instrument's volume envelope does), the NES's classic tricks (chords as
`0xy` arpeggios, an echo, a duty change, a vibrato that comes in late,
slides and a sampled kick), the VRC6 (its two pulses, and the sawtooth as
the bass and then the lead), this FamiTracker's own (the VRC6 Plus's soft
layer, stereo with `8xx`, the mixer's Soft and Space), and ten sound
effects for a game (a menu blip, a coin, a jump, a laser, a power-up,
footsteps, a switch, a hit, an explosion and a 1-up, a bar each at speed 1,
each made the NES's way by its instrument's sequences, with the pulse's
sweep `I13`, a slide `218` and a volume slide `A05`; its walkthrough has
the user change a sequence, an effect and notes and hear the difference,
then tells how to make your own and get them into a game as tracks that
halt with `C00`). A walkthrough
points at a channel's column, says what to listen for and plays a few bars
with a button in its note, often with one part alone; the songs and
walkthroughs are in `res/examples`, built into FamiTracker.exe. The first
tutorial ends by pointing at them.

## 7. Problems panel

Done (October 2026, on `next`): **View > Problems** shows what Claude
Code's `check_song` finds, for people working by hand, in a panel docked
along the bottom of the main window (right-click it to dock it on the right
instead; drag its edge to make it bigger). It comes back when FamiTracker
starts if it was open.

- Notes that vanish, notes that click, clashes, notes out of a channel's
  range, silent and busy bars and notes outside the key, grouped by kind
  with how many of each, the ones heard worst first, then what drafts miss
  in the arrangement (a melody in one-bar pieces, a thin or doubled bass, a
  sound its channel can't play, faders doing the mix, a repeat that copies,
  a lively stage's short loop). A click on a kind folds or unfolds it.
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
  Instrument button gives them a sound that lasts. **Fix**, on one of the
  arrangement's findings whose fix is a single call (a bass line, a
  doubled bass cleared, a sound the channel can play, the faders back at
  0 dB and a mix, a repeat varied), makes that call: one Ctrl+Z.
  **Explain** says what's wrong with the rest (a clash, a note outside the
  key) and what helps, and for a fix of several calls (a melody joined
  into phrases, a longer loop) gives the calls, for Claude to make.
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

## 10. Mixer

Done (October 2026, on `feat-mixer`): **View > Mixer**, a strip for every
channel along the bottom of the window (see `MCP.md`, The mixer).

- Each strip: the channel's name and chip, a meter while the song plays,
  mute and solo in step with the pattern editor's, a fader from -24 to
  +6 dB and a starting pan. Every change is one Ctrl+Z (a drag one for the
  drag).
- The faders and pans are saved with the song (a `BF MIXER` block, written
  only when a channel is off its default, so other songs save as before)
  and played by FamiTracker's mixer in playback, WAV files and the game's
  OGG files. A pan is the channel's before any `8xx`.
- NSF files don't have them: NSF export says so, and **Write into the
  notes** puts the faders into the volume columns and the pans as `8xx`,
  in every track, as one Ctrl+Z.
- Claude Code reads them in `get_song`, sets them with `set_mixer` and
  counts the faders in its loudness math.
- File > Export for games has a Loudness choice (as loud as it goes, or a
  calm place, most tracks or action at -22, -20 or -19 LUFS), so the
  game's tracks can sound equally loud.
- Done (October 2026, on `feat-space`): depth without using up channels.
  Each strip has two knobs, Soft (a gentle low-pass that rounds a pulse's
  or a sawtooth's edges) and Space (a send to one reverb with an echo that
  the whole song shares: a small room, a hall, a cathedral, a cave or the
  echo alone, the echo in beats of the track's tempo, 1-8 repeats). Both
  are off unless turned up, not the NES's own, so NSF files leave them out;
  playback, WAV files and the game's OGG files have them, the loops with
  the space's tail carried into their start. Claude sets them with
  `set_mixer`, sparingly.

## 11. VRC6 Plus

Done (October 2026, on `feat-vrc6plus`): a chip of this FamiTracker's own,
picked in **Module > Module properties** like the others: the real VRC6
(two pulses and the sawtooth) with a soft layer of four wave channels for
pads, a choir, a soft lead or a sub bass. It still sounds like the NES
VRC6, with a bit of (optional) softness and depth: the soft layer only
plays soft waves through the Namco 163's emulation (see `MCP.md`, VRC6
Plus).

- Its box takes the place of the VRC6 and N163 boxes while it's ticked
  (they show ticked and greyed); another chip (FDS, VRC7, MMC5, 5B) can
  still join it. Unticking it goes back to the plain VRC6, asking before
  the soft channels' notes are cleared.
- Its channels are named as one chip everywhere: Pulse 1, Pulse 2 and
  Sawtooth of the VRC6 Plus, then Soft 1 to Soft 4.
- Choosing it adds what's missing of its starter sounds: choir pad, string
  pad, soft flute and sub bass, each with a wave of its own. The sound
  browser lists them under VRC6 Plus.
- Underneath, the song is VRC6 + N163 with 4 channels, marked with a small
  `BF VRC6PLUS` block: other FamiTrackers open it as that and play it the
  same. Like any song with two expansion chips, it goes to a game as OGG
  files; NSF export plays one chip.
- Claude Code knows it: `set_expansion` takes "VRC6 Plus", and the tools
  keep the soft layer under the VRC6 and reach for it when you ask for
  something softer, warmer or deeper.
- Done (24 October, on `feat-calm-sounds`): sounds for the soft layer in
  the shapes it was missing. Four more built-in soft sounds, added by name:
  a breath (a lead that breathes in over 11 frames and sinks a little,
  then holds), a quick pad (in within 7 frames, for a chord every beat), a
  pulsing pad (struck again every 12 frames, a soft echo after it) and a
  music box (gone in 40 frames). `design_instrument` takes the soft
  channels too: a built-in sound's waves with an envelope of your own. A
  song holds at most 8 soft-layer sounds, 4 of them the starters, so the
  new ones come by name rather than with the starters. `reference_sound`
  still doesn't take its channels.
- Done (25 October, on `feat-soft-references`): `reference_sound` takes
  the soft channels. A studied sound becomes a wave sound there with the
  original's envelopes: an N163 game's or module's sound with its own
  waves (studies keep them now), another chip's with a wave from its
  channel (its duty's pulse, a wave for each width of a duty click, the
  triangle, the sawtooth, an FDS sound's wave). Each gets a wave place of
  its own, and the sound browser and `find_sound` list the studied sounds
  for the soft layer.

## 12. Ideas from Mario Paint

Dropped (the user, 26 October 2026: "Let's remove the mario paint idea. The
Piano Roll is fine as it is"). It was a stamp mode for the piano roll after
Mario Paint's music composer: pick a sound, click to place it, FamiTracker
finds the channel.

## 13. Views you can switch on and off

Done (October 2026, on `feat-views`; the user's idea of 24 October): every
way of placing notes has a check mark in the View menu.

- **View > Pattern editor** hides the channel grid, and the piano roll fills
  the space it leaves. There's always one view to place notes in: unticking
  the pattern editor while the piano roll is hidden shows the piano roll, and
  unticking the piano roll while the pattern editor is hidden brings the
  pattern editor back (so either menu item switches between the two).
- **With the grid hidden** the shortcuts keep working: play, stop, play from
  the cursor, the octave and instrument keys, undo and redo, saving. The
  note keys play notes in the piano roll, and with editing on (Space) type
  them at its cursor line a step apart, as in the pattern editor (the
  piano roll's letter keys for its tools are notes then; its buttons still
  pick the tools). Edit > Cut, Copy, Paste and Delete work on the piano
  roll's selection.
- **View > Drum grid** is a panel now (it was a dialog): docked under the
  piano roll or, from its right-click menu, on the right, beside the song.
- **The layout is remembered**: which views and panels show, the piano
  roll's height and the drum grid's place and size come back when
  FamiTracker starts.
- The tutorials show a part a step is about if it's hidden (the pattern
  editor too), and point at the drum grid's panel.

## 14. Calm and lively tracks (Claude Code)

Done in October 2026 (the user, 24 October, after the first night song on
the VRC6 Plus: "It finally manages to get melancholic and atmospheric
non-aggressive songs done"). What made it calm was written into the
composers' brief by hand; the tools do it now when a track is calm. Calm
applies only when asked (the user: "depends on the song"), or when the
track's name says so; a slow song without drums gets a line suggesting it.
Action and bosses keep the soft mix and polish's accents. See MCP.md,
"Calm and lively tracks".

- Done (on `feat-calm-defaults`): `mix` style calm and lively, as heard:
  each part's loudness estimated as its stem measures it (envelopes,
  widths, waves, faders, pans and Soft counted, checked against
  FamiTracker's renders), the melody 2.5 dB over the chords (lively 3.5),
  an echo 7 under, the bass 2.5 (the triangle by the DPCM level); the
  reply gives the balance reached. (The lively places were reset on
  `feat-lively-fix`: see 15.)
- Done (on `feat-calm-sounds`): lead sounds with calm shapes, ready made:
  `design_instrument` kinds and presets music box (struck, gone in 0.9 of
  a beat at the song's tempo), breath (in over 12 frames, sinking to 60%
  and holding) and falling strike (struck, falling to a low hold), with a
  faint vibrato (about 3 cents) that never clicks; the same shapes on the
  VRC6 Plus's soft layer.
- Done: `polish` style calm without accents, scoops or falls, its vibrato
  the shallowest (`451`) about half a second into long notes.
- Done: `check_song` and the review warn about a lead that holds a high,
  steady level (a finding of its own, `held`; on calm tracks only since
  `feat-lively-fix`, see 15),
  and the review on a calm track hears the balance as the stems would.
- Done: the mix's flaws the night songs found: a 5B lead counted as its
  sound holds, the 5B's buzz and noise swells kept as written, `levels`
  by the volume column, a part's level over some bars kept from the rest
  of the song, stepped DPCM levels moved together; the review counts the
  mixer's starting pans and the triangle's fader, tips an intro more than
  10 dB under the rest, and doesn't take one deep triangle note for the
  bass.

## 15. Knowledge in the tools (Claude Code)

Started on 24 October 2026 (the user's OK at 22:30): the knowledge that
composers read in a 400-line brief goes into the tools, so that Claude
composes as well at Extra High effort as at Maximum, and local models
(Qwen through Hermes) get a fair chance. A night song cost a composer
about 450,000-530,000 tokens and 60-120 tool calls, much of it reading
rules and fixing tool output by hand.

- Recipes per kind of song: `compose_track` takes `kind` (calm, lively or
  action), and `get_recipe` gives the same on every connection, the small
  set's too, whose steps name only that set's tools. The guide has the
  numbers as the topics `calm`, `lively` and `action`.
- The user's rules for the kinds (24 October, corrected on 25 October):
  calm only when a song is asked to be calm (the tools only suggest it
  when a song looks calm), and the calm rules (a lead that never holds a
  level, the melody 2-3 dB over the chords, no accents, no drums) for calm
  songs only. The user, 25 October: "Songs too active and direct was for
  the songs where I said it needed to be calm and melancholic for the
  night songs", and the night songs worked. Lively stages and action go
  back to the way of the first 30 tracks, which the user liked nearly all
  of: a lead with accents and steps by section inside a band louder than
  it, a sampled kick and snare, sections that build to a climax 2-4 dB
  over the first A.
- Done (on `feat-lively-fix`, 25 October): the night songs' rules taken
  out of lively stages. Measured on the first batch's lively tracks
  against the later ones (52-57), the later ones had lost the lead's
  dynamics (15 on every note where the first batch's sat at 8-9 with
  accents), the band under the lead (the bass about level with it and the
  band 3.3 dB louder, where the later lead sat 3.5 dB over its bass and as
  loud as the rest), sections that build (3.2 dB from the first A to the
  climax, against 0.95), the sampled snare and the lead's sound. `mix`
  style lively now mixes a stage's band (the melody's loudest at 9, the
  bass 1 dB under it, the chords together 3.5 under, the drums up or down
  to 10 under), `polish` style lively keeps its accents, the `held`
  finding is for calm tracks only, the review tips a stage that doesn't
  build, a lead at one volume and a melody over its band, `measure_mix`
  checks the band, the recipes, the style guide and `what_next` give the
  first batch's lead (`bloom lead`, not struck again), lush arpeggios of
  two frames a note and a sampled backbeat, and the `lively` groove is for
  gentle places.
- Calm tracks by default (14) when asked, the tool flaws the composers
  found, and `measure_mix` (stems, each section's loudness, the intro and
  the two sides in one call) on branches of their own.
- Next: a track composed at Extra High against one at Maximum (53
  Driftwood Rapids), then fewer tokens a song and better tracks.
- Done (on `feat-stage-fixes`, 25 October): what three composers of 53
  Driftwood Rapids found on a lively stage. `review_arrangement` hears
  whether the melody breathes (a rest of a beat or a held note in its
  two-bar spans; the version the user heard as "no clean melody" paused in
  4 of 16, the two liked ones in 9 and 13); the lively and calm mixes put
  the drums 10-12 dB under the melody and never raise them (the lively
  mix sets them to their place again since `feat-lively-fix`); `add_drums`
  has a `lively` groove (soft hi-hats, a soft kick, no snare; for gentle
  places since `feat-lively-fix`) and a light
  intensity that keeps the hi-hats; voiced chords on the N163 give each
  voice its own sound; broken chords on a pulse stay over C-2;
  `read_notes` reads 64 bars and says which bars it read; `vary_part`
  embellishes melodies of short notes. Then five more the user approved:
  a change to minor keeps a dominant's ninth natural, `add_harmony` steps
  off semitones against a pad, and `write_notes` keeps the volume of the
  notes it replaces and the bars its notes leave empty; `vary_part`'s
  ending fits the chord under it. And three the lead added: `add_harmony`'s
  thirds and sixths are the key's own (a harmonic minor's too), over the
  chords as FamiTracker plays them; `add_echo` and `check_song` agree on
  which semitones rub (an echo F-4 over a pad's E-2 too); the breath check
  counts the spans the melody plays in and a held note in the span it
  starts in (a desert stage's draft paused in 3 of 16, its rewrite in 15).
- Done (25 October, on `feat-soft-references`): `measure_mix` renders the
  mix and every part alone in one pass, each part exactly as before, in
  about half the time (a 40-bar track with nine parts).
- On the backlog (the user's idea, 24 October): Claude Fable 5.1 for the
  knowledge work, the one-time work every later song reuses (what makes a
  track good, turned into the tools, the recipes and the guide), rather
  than for composing each track. On a Max plan Fable draws on the same
  weekly limit (at most half of it) and costs about 2.5 times Opus 5.5 a
  token, so it pays off where its result is reused.
- Local models (25 October 2026): the user's local model (Qwen 3.8 Flash
  Next, through Hermes Agent) composed "Green Valley" with the full set of
  tools. It was clean on every check, but its melody came in one-bar pieces
  (a release before 23 of its 28 bar lines), its bass was thin, its drums
  loud and its loop short (52 s); a refiner fixed those by hand. Done:
  melodies in steps (`write_notes` and `read_notes`). Being built, the
  user's OK: `what_next` (the one next step with its tool call) and the
  small set fitted to how a local model works (with `find_sound`,
  `reference_sound` and a worked example of a melody); checks for what it
  missed, each with a ready fix call, and short replies; a melody grown
  from a motif. Then a "finish" call (polish, mix, the loop checked and
  fixed, the export at the kind's loudness) and warnings as `write_notes`
  writes (a register too high or low for the channel, notes outside the
  key, a lone short note on the triangle).
- On the backlog (the user's OK, 25 October):
  - You hum, the local model arranges: the phone page brings the user's
    melody in as a clip, and the model places it and builds the chords,
    bass, drums and depth around it. The melody is a local model's weakest
    part and the user's strongest.
  - A fixed test set: three to five requests (a calm night, a lively stage,
    a boss fight) run on the local model after each change, scored by the
    review, the checks, the melody's breaths and the balance, to see what
    actually helps.
  - Learning from the good songs: the soundtrack's tracks and the tool calls
    of the composers who made them as examples for the local model, a few
    in its prompt or as data for a LoRA on the user's EVO X3.

## 16. Help for newcomers to music

The user (26 October 2026), asking what else would help people new to music
use this FamiTracker; their own idea was a glossary. Agreed the same day,
each with the user's words:

- **A glossary you can show and hide** ("a toggle to view it/hide it ... a
  modal that is non-blocking"; the main window is busy enough): a window of
  its own that stays open beside the song without blocking it, opened and
  closed from the menu, a toolbar button and a key. It covers "some basic
  music/composing terms so a newcomer can start making music and
  understands the terms" (notes, octaves, scales, keys, chords, melody,
  phrases, rhythm, bars, tempo, the song's parts), the NES's chips and
  sounds, the tracker's own words (rows, patterns, frames, instruments,
  sequences) and every effect, each in plain words with a Hear it button
  that plays a short example. The same entries reach Claude and local
  models, so they explain with the same words.
- **Effects explained in place** ("absolutely!"): hovering an effect in the
  pattern editor shows what it does with its own numbers ("vibrato: speed
  4, depth 2"), and Hear it plays it.
- **A What next panel** (yes): `what_next` in FamiTracker, docked like the
  problems panel: the one next step in plain words, why, and a button that
  does it.
- **Hear the problem** (yes): the problems panel's findings get Play it
  (the bars where it happens) and, where there's a fix, Play the fix (kept
  or taken back).
- **Chords with roles** ("yes ... but that should be a toggle maybe"): the
  piano roll's chord strip colors each chord by what it does (home, away,
  tension, back home) and gives it a feel word, switched on and off from
  the piano roll.
- **Strong notes on the beat** (yes): the piano roll lights the notes of
  the chord sounding under the cursor, the safest notes on a beat.
- **Song shapes in one click** ("yes!"): a command that lays out a song's
  named sections for its kind (a lively stage, a calm night, a town, a boss,
  a short loop), each with a line on what it's for.
- **Small exercises with feedback** ("yes! A good way to ease in the new
  composer!"): short tasks in the tutorial window (write four bars that end
  on the home note, a bass on the roots, a beat, a phrase that breathes),
  checked by the tools, with friendly feedback and the glossary's words.
  After the glossary, the panels and the song shapes are in.

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
  - More natural input (October 2026, `feat-recorder-input`): Edit > Start
    from a groove (a mood, a tempo and a key, and drums, a bass line and
    chords to sing the melody over, which play during the count-in and the
    take and stay in the song); a take sung over the selected rows only,
    the song playing a bar before; tapping the tempo instead of setting
    speed and tempo; chords typed by name ("C G Am F") or strummed on a
    guitar for Add chords and bass line.
  - Ideas from the phone (October 2026, `feat-phone-takes`): Edit >
    Record from your phone shows a QR code; the phone opens a page
    FamiTracker serves on the home network, records a hummed idea (or
    takes the phone's own recording) and sends it, and it becomes a clip
    of the open song, named after the idea. See `RECORDER.md`, Ideas from
    your phone.

  The steps planned for the recorder are all done now.
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
  - Fixed after the soundtracks studied in October 2026: a sampled bass on
    the DPCM (Journey to Silius) is a bass, its samples on the keys of the
    notes they sound, not a drum kit; the loop is what keeps on repeating
    (not a passage played twice in a piece that ends, nor a short vamp in a
    long loop), from the start when the first time through differs only too
    quietly to hear, and found when the drums cycle on their own; a track
    that falls silent long before its playlist length is a sting or a sound
    effect; the key is weighed by the chords, the bass and where phrases
    rest, and "no clear key" when the chords drift.
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

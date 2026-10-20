# Roadmap: beginner-friendly FamiTracker

Branch `beginner-friendly`. The goal is a FamiTracker that someone with no
music training can use to write VRC6 chiptune music for a GameMaker game,
which plays the tracks as OGG files.

## Principles

- **Keep songs readable everywhere.** The piano roll is another view of the
  same pattern data, profiles live in program settings, and presets are
  ordinary `.fti` files. Songs must keep opening in vanilla FamiTracker,
  0CC/Dn-FamiTracker and FamiStudio. The one addition to the `.ftm` file is
  the recorder's clips, in a block of their own (`BF CLIPS`) that other
  versions skip; there, linked copies are ordinary notes.
- **Stay in the UI and export layers.** The NSF driver is binary-only in this
  repo (see CLAUDE.md), so no feature may need new driver commands.
- **Full FamiTracker stays one click away** through a "Show everything" option.

## Where things stand

Everything built so far is in the `beginner-friendly` build, the "Latest"
download (`build-beginner-friendly`): the foundation, recording, clips, the
piano roll, the export for games, sound effects and the Claude Code
connection. Most of it was built on the `recorder` branch (see Feature
branches) and merged in October 2026.

Next: the steps below that make FamiTracker itself simpler (3, 4, then 6),
which haven't been started.

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

## 3. Sounds before settings (not started)

- Instrument library: curated `.fti` presets (VRC6 saw bass, pulse leads and
  pads, noise kick/snare/hat) with names, short descriptions and
  click-to-preview. Use `CFamiTrackerDoc::LoadInstrument` to load and
  `CSoundGen::QueueNote` to preview. The presets themselves exist for Claude
  Code (`Source/Api/Presets.cpp`: 13 for the 2A03, MMC5 and
  VRC6, plus the VRC7's 15 built-in sounds), but FamiTracker's own UI can't
  browse or preview them yet.
- Drum lane: a 16-step grid that writes notes to the noise channel.

## 4. Profiles (not started)

- A profile bundles a module template (chip, tempo, rows, effect columns,
  highlights), the set of visible UI and starter instruments.
- The default profile, "VRC6 game music", turns VRC6 on, keeps one effect
  column, hides DPCM and the advanced menus, and shows BPM instead of
  speed/tempo.
- Users can customize a profile with a checklist, and "Show everything"
  restores the full UI. Settings live in `CSettings`; menus and toolbars are
  set up in `CMainFrame`.

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

## 6. Tutorials (last, once the UI stops changing)

- A coach-mark overlay that dims the window, highlights the next control and
  advances when the user acts. Steps are stored in a data file, so new
  tutorials need no recompile.
- Annotated example songs, since module comments can already pop up when a
  file is opened.

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
    the NES would), and Claude Code's `make_sound_effect` (25 kinds with
    variants, or a recording).

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
  - sound effects for the game, from 25 kinds or a voice recording.

  Ideas not built yet: instruments described in words, FDS and N163
  instruments, a soundtrack guide (main motif, house sounds, keys per area),
  song sections, named versions to compare, listening help (loop, solo,
  mute), and recording through Claude.
- **`nsf-import` (planned):** pull instruments, and optionally songs, out of
  NSF files.
  - Start from the archived `nsf-importer` fork (NSF Importer 0.1, based on
    0.3.6). It plays the NSF in an emulator and reads the chip registers every
    frame.
  - Turn each note's volume, duty and pitch movement into FamiTracker
    macros.
  - Where the source sound can't be reproduced on NSF + VRC6, use the closest
    match.
  - VRC6 game rips give real VRC6 instruments.

## Open questions

- Some recordings "don't sound correct" (maybe background noise). Waiting
  for a sample take from `Documents\FamiTracker recordings` to diagnose.

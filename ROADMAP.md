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

## 1. Foundation (in progress)

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
- [ ] x64 build.

## 2. OGG export with loop points

- Render each song to OGG Vorbis alongside the existing WAV export
  (`CSoundGen::RenderToFile`, `CWaveFile`, `CCreateWaveDlg`,
  `CWavProgressDlg`). Rendering is currently mono WAV only.
- Find the loop from the song's `Bxx` jump target, and record a
  sample-accurate loop start and end. Render far enough past the loop that
  effect and sequence state has settled.
- Show and copy the GameMaker calls `audio_sound_loop_start` and
  `audio_sound_loop_end` (GameMaker 2023.2 and later; confirm the units in the
  manual).

## 3. Sounds before settings

- Instrument library: curated `.fti` presets (VRC6 saw bass, pulse leads and
  pads, noise kick/snare/hat) with names, short descriptions and
  click-to-preview. Use `CFamiTrackerDoc::LoadInstrument` to load and
  `CSoundGen::QueueNote` to preview.
- Drum lane: a 16-step grid that writes notes to the noise channel.

## 4. Profiles

- A profile bundles a module template (chip, tempo, rows, effect columns,
  highlights), the set of visible UI and starter instruments.
- The default profile, "VRC6 game music", turns VRC6 on, keeps one effect
  column, hides DPCM and the advanced menus, and shows BPM instead of
  speed/tempo.
- Users can customize a profile with a checklist, and "Show everything"
  restores the full UI. Settings live in `CSettings`; menus and toolbars are
  set up in `CMainFrame`.

## 5. Piano roll

Done on the `recorder` branch (see `RECORDER.md` there): a horizontal view
under the pattern editor, time left to right and pitch bottom to top, that
follows playback, with drawing, painting a melody, moving, copying,
resizing and erasing notes, a key that new notes snap to, and clips placed
with the mouse. What was planned for it:

- **v1:** one channel at a time. Draw, erase and resize notes with the mouse,
  with scale lock (pick a key and scale, off-scale rows are dimmed and notes
  snap), a playback cursor and undo. Drawing a melody's shape in one stroke,
  snapped to the key and the beat, gives people who can't hum in tune a way
  in too.
- **v2:** ghost notes from other channels (FL Studio style), and a chord helper
  that spreads a chord across free channels or turns it into a `0xy`
  arpeggio. Mood starters lay down chords, bass and drums for moods like
  "cozy town" or "boss fight".
- **Implementation notes:**
  - It is a new view beside `CPatternEditor`, and all edits go through
    `CPatternAction` so undo works.
  - A note's length is the number of rows until the next note, release or halt
    in that channel.
  - Each channel plays one note at a time.

## 6. Tutorials (last, once the UI stops changing)

- A coach-mark overlay that dims the window, highlights the next control and
  advances when the user acts. Steps are stored in a data file, so new
  tutorials need no recompile.
- Annotated example songs, since module comments can already pop up when a
  file is opened.

## Feature branches

Larger ideas get their own branch made from `beginner-friendly` and merge back
when ready.

- **`recorder` (in progress):** whistle, hum or tap and get notes whose
  length matches the sound, either recorded in FamiTracker (a toolbar button,
  with a count-in and click at the song's tempo) or from a WAV file.
  Recordings and pattern selections become reusable clips. See `RECORDER.md`
  on that branch. Making the result sound more natural and harmonic goes in
  this order:
  1. Expression (done): loudness, slides into notes and vibrato from the
     performance, and a richer lead sound.
  2. Harmony backing (done): find the key, pick chords that fit the melody
     bar by bar, and write a bass line and arpeggiated chords.
  3. AI arranging over MCP, through the `api` branch below.

  Clips and the piano roll are done: takes and pattern selections kept with
  the song, placed as copies or linked, with a clip editor; notes drawn,
  painted and moved in a piano roll under the pattern editor. The order from
  here (the MCP connection was started early, on the `api` branch):
  1. More natural input, built on the recorder: start from a groove (pick a
     mood, tempo and key, get a few bars of drums, bass and chords, and sing
     the melody over them); re-sing just a selection (the song plays a bar
     before, the take replaces only those rows); tap the tempo instead of
     setting speed and tempo.
  2. Sound effects by voice ("pew", "boing", "kssh"), exported like the
     music, and chords by name ("C G Am F") or strummed on a guitar.
  3. AI arranging over MCP (the `api` branch).
- **`api` (planned):** a local HTTP/JSON API to the running FamiTracker,
  bound to `127.0.0.1` and opt-in. It would cover song info, pattern cells,
  instruments, play/stop and save/export, and run requests on the UI thread
  through the undo system. A separate MCP server would wrap it for agentic
  workflows, such as asking an assistant to arrange a recorded melody ("add
  chords and a bass line, cozy village style"). A first version could use
  whole-song text export/import plus transport controls. Decided so far:
  - The client is Claude Code on the same PC.
  - Claude works in musical terms: bars, beats, note names, instruments, and
    the key and chords from the recorder's harmony code, not hex rows. It
    writes notes, adds instruments from sound presets ("soft flute lead",
    "plucky bass", "noise snare"), sets the tempo, plays a range and exports
    a looping OGG.
  - It can't hear, so it gets checks: notes outside the key, clashes between
    channels, notes outside a channel's range, parts too busy or too empty.
  - By default its ideas arrive as clips to listen to and place; a setting
    lets it change the song directly, always undoable.
  - Mood changes ("make it darker", "happier", "like an underwater theme")
    combine tools that work reliably on their own: change the key or mode,
    the tempo, the sounds, add an echo channel or vibrato. The result is a
    clip or a new version of the song to compare.
  - A small page on the phone records ideas over Wi-Fi into the clips.
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

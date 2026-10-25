# Recorder: whistle, hum or tap, and get notes

Built on the `recorder` branch and merged into `beginner-friendly`, whose
build is the "Latest" download. Record a melody (whistle, hum, sing) or a
rhythm (finger taps, beatbox) and get FamiTracker notes, add chords and a bass
line that fit it, and keep takes and pieces of the song as clips to place
again, as copies or linked.

## Requirements

- **A note lasts as long as the sound that made it.** A long hum becomes a long
  note. The lead instrument holds its volume until a note release (`===`) where
  the sound stops, then fades out over about 80 ms instead of stopping dead.
  Breaks shorter than 150 ms between two notes are held through, the way a
  musician would write them. `TestNoteLengths`, `TestQuietEdges` and
  `TestQuantizeRules` cover this.
- Output stays plain FamiTracker pattern data, so songs open in any
  FamiTracker. The one addition is the song's clips, saved in a block of
  their own (`BF CLIPS`) that other versions skip; there, linked copies are
  ordinary notes.

## Status

- [x] Detection engine in `Source/Transcribe/`: plain C++14 with no
  Windows/MFC dependencies. It is also compiled into `FamiTracker.vcxproj`,
  where Insert recording and Record from microphone use it.
  - WAV input (PCM 8/16/24/32-bit, float; stereo is downmixed).
  - YIN pitch tracking from 70 Hz to 4.2 kHz, covering a low hum up to a high
    whistle.
  - Note segmentation. It bridges dropouts of up to 40 ms and keeps vibrato and
    short glides inside one note. Singing gets a note per syllable ("la la
    laaa"), even on one pitch.
  - Octave fit, scale snap, and row quantization using FamiTracker's own tempo
    math.
  - Tap detection, sorting hits into kick, snare and hi-hat by brightness.
  - A FamiTracker text-module writer, plus a validator ported from
    `CTextExport::ImportFile`.
- [x] `wav2ftm` command-line tool. CI builds it as the `wav2ftm-Win32`
  artifact. Import Text always replaces the open song, so `wav2ftm` combines
  a melody, a drum part and a bass line from separate recordings into one
  song (`--drums`, `--bass`).
- [x] Tests: 667 checks, mostly on synthesized audio, covering:
  - pitch of hums and whistles, and telling them and taps apart,
  - note lengths, quiet note endings, rounding to rows and short breaks,
  - sung syllables: repeated ones on one pitch, scoops, glides and drifts
    inside a syllable, vibrato and swells that are not syllables,
  - repeated notes and glides, slides, loudness and vibrato,
  - octave fit and scales,
  - taps,
  - the exact text cells, including songs with several parts.
  - the FFT difference function against the direct sum,
  - the click, finding it in a take recorded on speakers and taking it out
    of drum hits, measuring the delay of taps, and lining takes up with rows,
  - keys, bars, chords for known tunes, the bass line and the arpeggios,
  - sound effects from a voice ("pew", "kssh", "boing"), and when a take of
    one is over,
  - tapped tempos (missed taps, double taps, pauses), the click in the
    song's bars, chords with more notes than three and over a bass note,
    and strums: guitars and a piano synthesized string by string, their
    onsets and the notes that sound together,
  - a last note held past the end of the song ending on its last row.

  Also checked by hand on noisy recordings with room echo, pitch scoops and
  timing wobble.
- [ ] Tuning on real recordings. Round one, a 34-second whistle: notes sounded
  cut short. Quiet note endings were lost under the silence gate, rounding
  start and end separately cost up to 80 ms per note, short breaks became
  hard cuts, and two notes on one row lost the second one's length. All four
  are fixed. Before, 11 of 40 notes in that recording stopped more than 50 ms
  before the whistle did; now, counting the release fade, none do.
  Round two, 11 seconds of "la la la" singing: repeated syllables on one
  pitch became one long note, and scoops and drifts inside a syllable became
  extra short notes (47 notes in all). Syllables are now notes of their own
  and scoops are slides (35 notes, one per syllable); whistled recordings
  come out exactly as before.
- [x] Edit > Insert recording (WAV) in FamiTracker, also in the pattern
  editor's right-click menu (`Source/InsertRecording.cpp`). A dialog
  (`InsertRecordingDlg.cpp`) asks what the recording holds, preselected with
  FamiTracker's guess (whistling, humming or singing, taps or beatboxing), and
  which channel it goes on: the cursor's channel for a melody, the noise
  channel for drums. It writes the notes into the open song from the cursor
  row on. It uses the song's
  speed and tempo, and runs on into the following frames, adding frames at
  the end of the song if needed; a last note that only rings a little past
  the end ends on the song's last row instead, so the song keeps its length
  and its loop. A touched frame whose pattern other frames
  also play first gets a copy of its own, so the rest of the song doesn't
  change. It adds Lead, Bass, Kick, Snare and Hi-hat instruments the first
  time and reuses them after that; on VRC7, FDS and N163 channels it uses
  the selected instrument. Notes end with `===` when the instrument has a
  release, otherwise with `---`. One Ctrl+Z undoes all of it, except that
  added instruments stay (instrument edits aren't undoable in 0.4.6).
- [x] Expression, so a melody keeps how it was played (`Expression.cpp`, on by
  default; the Insert recording dialog has a checkbox, `wav2ftm --plain` turns
  it off):
  - loudness: each note gets a volume column value from its median level,
    F for the loud notes and one step lower for every 3 dB quieter, never
    below 6;
  - slides: short pieces whose pitch glides into the next note (a scoop up to
    a whistled note) become a slide into it, `Qxy` up or `Rxy` down, with the
    speed worked out from the slide's length and the channel's period table;
  - vibrato: a performed 4-8 Hz vibrato is measured and kept (`4xy`); other
    notes of half a second and longer get a gentle delayed one (about 5.6 Hz,
    12 cents, from a quarter second in), except on bass channels. `400` on the
    note's last row stops it.
  - richer sound: the lead swells in like a whistle (`11 14 15 15 14 13 12 11
    10`), and on the VRC6 its duty cycle shimmers slowly between 4/16 and
    3/16. The bass keeps the punchier envelope.
- [x] Record from microphone in FamiTracker (`RecordDlg.cpp`, with the audio
  in `MicRecorder.cpp`): the microphone button on the toolbar, Edit > Record
  from microphone, the pattern editor's right-click menu, or Ctrl+Shift+R.
  - Pick what you'll record, the channel and the microphone. A level meter
    shows what the microphone hears, with "Too loud!" and "No sound?" hints.
  - The click counts in one bar at the song's tempo (a beat is the first row
    highlight, a bar the second), then clicks along on every beat or once a
    bar. It can also count in only, or not click at all. The click is a
    short, soft "tock" (C6 on a bar's first beat, G5 on the others, fading
    within a few milliseconds), and its volume slider plays it when you let
    go, so you can hear the level before recording.
  - "Play the song along" plays the song from the cursor while you record,
    starting so its first row sounds with the downbeat. The channel being
    recorded stays quiet, so an earlier take doesn't play along.
  - Stop puts the take in at the cursor row, the downbeat after the count-in
    on that row, as one undoable action, and saves it as a WAV file in the
    `Recordings` folder next to FamiTracker.exe (takes saved before in
    Documents\FamiTracker recordings move there by themselves; where
    FamiTracker's folder can't be written, they stay in Documents).
  - Test timing: tap along to eight clicks, and takes are lined up with how
    late the taps arrived (see How it works).
  - "From a WAV file..." switches to Insert recording.
- [x] Chords and a bass line (`Harmony.cpp`): Edit > Add chords and bass
  line, also in the right-click menu, and `wav2ftm --backing`.
  - Pick the channel with the melody; the key is found (or picked), and the
    preview shows the chords bar by bar.
  - The chords go on VRC6 Pulse 2 (without the VRC6: 2A03 Pulse 2) as fast
    `0xy` arpeggios, with a soft, thin Chords instrument; the bass line goes
    on the sawtooth (or the triangle) with the Bass instrument.
  - Bars and beats follow the row highlight, so a take recorded against the
    click lines up with them. Chords change once a bar, or up to twice.
  - It covers the bars the melody plays in and replaces what the two channels
    had there; Ctrl+Z undoes it.
- [x] Clips (`Clips.cpp`, `ClipsDlg.cpp`; see Clips below): a take, a WAV
  file or the selection in the pattern editor kept with the song, placed at
  the cursor as a copy or linked. The clip editor gives notes another
  instrument and transposes them. Placing a clip or a backing replaces the
  notes and effects there, but the song's speed changes, jumps, skips and
  halts (`Fxx`, `Bxx`, `Dxx`, `Cxx`) stay.
- [x] Piano roll (`PianoRoll.cpp`, `NoteEdit.cpp`; see Piano roll below):
  View > Piano roll shows the song's notes on a grid under the pattern
  editor, time from left to right, and follows playback and the cursor.
  Notes are drawn, painted as a melody with the mouse, moved, copied, made
  longer or shorter and erased, kept in a key if one is picked; clips are
  placed with the mouse. Effects are shown on the notes and put on them from
  a menu: vibrato, tremolo, chords, slides, glides, bends and fades. The
  Groove button lays down a mood's drums, bass line and chords. Every
  change is undoable.
- [x] Sound effects from your voice (`VoiceEffectDlg.cpp`; see below): Edit >
  Sound effect from your voice records "pew", "boing" or "kssh", stops by
  itself when the sound is over, plays it as the NES would and adds it as a
  track that plays once, for the export for games.
- [x] Ideas from your phone (`PhoneDlg.cpp`, `PhoneLink.cpp`,
  `Api/PhoneTakes.cpp`, the page `res/phone.html`; see above): Edit > Record
  from your phone shows a QR code of a page FamiTracker serves on the home
  network, and an idea hummed into the phone becomes a clip of the open song.
- [x] More natural input (October 2026; see Recording in FamiTracker):
  - **Start from a groove** (Edit > Start from a groove, or the button in
    Record from microphone): pick a mood (town, overworld, battle, boss,
    dungeon, sad, calm, chase, upbeat, shop, underwater, castle, desert,
    nostalgic), a tempo (typed or tapped), a key (the song's own when it has
    one, or for a mood of the other kind its relative minor or major, which
    has the same notes) and 4, 8 or 16 bars, and FamiTracker writes drums, a
    bass line and chords from the cursor's bar, the same groove Claude
    Code's `make_groove` writes, sets the song's speed and tempo and makes
    the key the song's (as the piano roll's Key menu would), unless the song
    has another already. One Ctrl+Z takes it out again. Then you record over
    it: the groove plays during the count-in and the take, and stays in the
    song as its own parts.
  - **The bar before:** with "Play the song along", the song starts a bar
    (the count-in) before the take, so you hear what leads into it. At the
    song's first row, a song that loops plays its last bar, the way it
    comes round again. The click's high "tock" stays on the song's first
    beats when a take starts in the middle of a bar.
  - **Re-sing just a selection:** select rows in the pattern editor (or
    notes in the piano roll: their bars count), and "Replace only the
    selected rows" makes the take replace just those rows of the channel.
    The rest of the song plays as before: a note sounding into the
    selection ends where it starts, and the note after it keeps its
    instrument, volume and effects. A note sung up to half a beat early
    counts as the selection's first. The song plays a bar before, and the
    take stops by itself when the selection is over.
  - **Tap the tempo:** the Tap button (or the T key) in Record from
    microphone and Start from a groove. Tap four times or more with the
    beat; when you stop, the song gets the nearest tempo whose rows all
    last as long (when one is within 3% of the taps), otherwise the tempo
    you tapped, with speed and tempo picked as Claude Code's `set_tempo`
    picks them. Ctrl+Z puts the old tempo back.
  - **Chords by name or strummed:** Add chords and bass line takes chords
    typed in its Chords box ("C G Am F", a chord a bar, or "C | G | Am F"
    with bar lines; sevenths, "sus", "C/E", "N.C.", "%" for the bar again,
    or degrees like "I vi IV V") instead of finding them from the melody,
    and then needs no melody. Strum them... records a strummed guitar (or a
    piano) against the click, From a WAV file... reads one, and the chords
    heard bar by bar (or half bar) fill the box, to check before Add.
    Record from microphone's "Chords strummed on a guitar or played on a
    piano" does the same and then opens Add chords and bass line.
- [x] Claude Code connection, first version (`MCP.md`): File > Connect
  Claude Code lets Claude Code read the song in musical terms, write parts
  (as clips by default, or straight into the song, undoable), add sounds from
  presets, transpose and change the mode, set the tempo, add chords and a
  bass line, an echo or vibrato, change the mood in one go, check the song
  and play it.
- [ ] More for the Claude Code connection, from the plan: a local
  server in FamiTracker that Claude Code connects to. Claude reads the song in
  musical terms (bars, beats, note names, key and chords), writes notes, adds
  instruments from sound presets, sets the tempo, plays a range and exports a
  looping OGG. It can't hear, so it gets checks instead: notes outside the
  key, clashes between channels, notes out of range, parts too busy or too
  empty. By default its ideas arrive as clips to listen to and place; a
  setting lets it change the song directly, always undoable. Requests like
  "write drums for my melody", "three variations of this hook", "an 8-bar
  intro", and mood changes: "darker" (minor, lower, slower, rounder sounds),
  "happier" (major, brighter, bouncier), "underwater" (slow 3/4 sway, soft
  sounds, wobbly vibrato, rippling arpeggios, an echo channel). Ideas can
  also come from the phone: a small page records over Wi-Fi into the clips.
- [ ] Turn a recorded sound into a DPCM sample with the existing WAV import.

## Recording in FamiTracker

1. Set the song's speed, tempo and row highlight first: the click follows
   them. With the default highlight (4 and 16), a beat is 4 rows and a bar 16.
2. Put the cursor where the take should start, usually on row 0 of a frame.
3. Press the microphone button on the toolbar (or Ctrl+Shift+R).
4. Pick what you'll record and the channel. Whistle a little and check the
   level meter: it shouldn't say "Too loud!".
5. Press Record, wait for "Get ready: 1 2 3 4", perform, and press Stop (or
   Enter).

To start a new song from nothing, use Edit > Start from a groove instead of
step 3: pick a mood, a tempo (or tap it) and a key, press Make the groove,
and the Record dialog opens over the groove with "Play the song along" on.
The groove plays while the click counts in. Sing a melody over it, and the
notes go on the channel the groove left free; a last note held past the
groove's end ends with it, so the song still loops as the groove did.

To sing part of a melody again, select its rows in the pattern editor (or
its notes in the piano roll) before step 3. The dialog says which rows; the
song plays the bar before them while counting in, the take replaces only
them, and it stops by itself at their end. Ctrl+Z gives the old notes back.

To set the tempo by feel, press Tap (or T) in time with the beat, at least
four times. The song's speed and tempo change when you stop tapping.

For chords, pick "Chords strummed on a guitar or played on a piano" and
strum along after the count-in, a chord or two a bar on the beat. Stop
opens Add chords and bass line with the chords it heard, bar by bar; change
any you like (they're names, like "Am" or "G7"), pick where the chords and
the bass go, and press Add. Or type the chords there yourself.

Use headphones, so the click and the song stay out of the take. On speakers,
FamiTracker finds the click in the take and lines the notes up with it, and
takes the clicks out of drum takes, but a song playing along can still end up
in a melody. If the notes come out late, press Test timing once and tap
along. If the meter says "No sound?", check that Windows' privacy settings let
apps use the microphone.

## Ideas from your phone

A tune often comes when you're away from the PC. Edit > Record from your
phone lets the phone keep it: hum it into the phone, and it arrives in the
song's clips.

1. In FamiTracker: **Edit > Record from your phone...** A small window shows
   a QR code and its address. Your phone and the PC must be on the same home
   network (the same Wi-Fi).
2. The first time, FamiTracker only listens to this PC: press **Allow
   connections from other computers on my network** (the same setting as in
   File > Connect Claude Code). Windows may ask whether FamiTracker may use
   the network: allow private networks.
3. Scan the code with the phone's camera. The page opens: a name for the
   idea, one big button and the takes sent so far. Bookmark it, or add it to
   the home screen, to record later without opening the window again.
4. Type a name, press the button, hum or whistle, and press it again (or, on
   the plain home-network address, record with the phone's recorder: below).
   The page sends the take, and a moment later it's a clip of the open song,
   named after the idea ("Idea 1" without a name), in the clips panel. The
   page's list says "In the clips as ...".

The take is transcribed as Record from microphone transcribes a take, but
without a click: its first note lands on the clip's first row, at the
selected track's speed and tempo, on the lead's channel (the first VRC6
pulse, or the 2A03's first pulse). Humming, singing and whistling become a
melody, taps drums. The song's patterns don't change: drag the clip where you
want it, or ask Claude Code to place it (`place_clip` takes it by its name).
The take is kept in the `Recordings` folder next to FamiTracker.exe as
"Phone <date> <time> <name>.wav". While FamiTracker is busy (a dialog is open,
say), the take waits there and the page says so; it becomes a clip as soon as
FamiTracker is free. The page makes a quiet take louder before it sends it
(up to 30 times), and takes up to 3 minutes; the window lists the last ideas
that arrived.

### The phone's microphone, and Tailscale

Browsers only let a page use the microphone on a secure address (https, or
the computer itself). The address in the QR code is plain http on your home
network, so there the button opens the phone's own recorder (on Android; an
iPhone may offer its files instead: record a voice memo first, save it to
Files, then pick it). The page reads what the recorder made (m4a, webm, 3gp, mp3), turns
it into a WAV file of one channel at 22050 Hz itself and sends that, so
FamiTracker only ever reads WAV files. To record right in the page:

- **Tailscale's https:** with Tailscale on the PC and the phone, and HTTPS
  certificates turned on for your tailnet, run `tailscale serve --bg 46600`
  on the PC once. Then open `https://<pc-name>.<tailnet>.ts.net/phone?code=...`
  on the phone, with the code from the window's address. FamiTracker must
  allow connections from other computers (it checks the name a request is
  sent to), and the PC's name on Tailscale must be its Windows name.
- **Chrome on Android:** in chrome://flags, add the window's address
  (`http://192.168.1.10:46600`) under "Insecure origins treated as secure"
  and restart Chrome.

The window's Network list also has this PC's name. With Tailscale on the
phone, a code of the name works wherever the phone is (Tailscale's MagicDNS
finds the PC by its name), and some home routers know the name too; it's
plain http, so there the phone's recorder records.

### Safe at home

- Every request needs the code in the QR code's address. It's a code of its
  own, worked out from the access code, so the address in the phone's history
  (perhaps synced to the cloud) can only send takes and list them, never drive
  Claude Code's tools. New access code (File > Connect Claude Code) changes
  it: scan the code again.
- A take must be a WAV file of 16-bit sound on one channel, half a second to
  3 minutes long; anything else is refused before FamiTracker reads it, and
  a name can't hold folders.
- With "Allow connections from other computers on my network" off, nothing
  but this PC is answered; with it on, only home networks' and Tailscale's
  addresses, never the internet. Other web pages can't send takes. See
  `MCP.md`, Over the network, safely.

## Sound effects from your voice

Edit > Sound effect from your voice turns a sound you make into a sound
effect for your game, a track of its own:

1. Press Record and make the sound: "pew", "boing", "kssh", a whistle. The
   take stops by itself once you've been quiet for a moment, or press Stop.
2. FamiTracker says what it heard and plays it as the NES would. Your
   voice's pitch and loudness go on a pulse channel, sliding between notes;
   the breathy and noisy parts (a "k", an "sh", a puff of breath) go on the
   noise channel, brighter for "sss" than for a breath. Listen plays it
   again, and Your recording plays the take.
3. Pitch moves it by whole octaves. "Where game sounds sit" puts its middle
   near E-4, where NES sound effects often are; "As you made it" keeps your
   pitch. Without "Breathy and noisy parts on the noise channel" it's a
   pulse alone.
4. Give it a name and press Add to the song (or Enter in the name box). The
   new track plays at speed 1, a row a tick, and halts at its end, so File >
   Export for games saves it as a sound that plays once
   (`audio_play_sound(snd_sfx_pew, 0, false)` in GameMaker). Its
   instruments, named after it, hold the shape in their sequences, so you can
   smooth it in the instrument editor.

Open a WAV file... uses a recording you have instead, and starts in the
`Recordings` folder next to FamiTracker.exe, where takes are kept as "Voice
<date> <time>.wav", so Claude Code can make another version from one later
(`make_sound_effect` with the recording `"latest"` takes the newest). A sound effect lasts up to 4.2
seconds (5 on PAL). Keep sound effects in a module of their own ("Sound
effects.ftm"), so the music stays clean.

Adding goes through Claude Code's `make_sound_effect` tool (see `MCP.md`),
so the dialog and Claude make the same sound from a take; it works without
Claude Code connected. A take begins when the sound rises 12 dB above the
microphone's own noise, as the meter heard it just before (at least -34
dBFS, at most -20); the first 0.15 s, where the click of the button is, are
left out, and it ends after 0.4 s at 30 dB below its loudest
(`transcribe::VoiceTake`).

## Clips

A clip is a piece of pattern data kept with the song, to place again: a
recorded take, a WAV file, or rows selected in the pattern editor (several
channels at once if the selection spans them).

- **Make one:** select rows and use Edit > Save selection as clip
  (Ctrl+Shift+C, or "+ Save selection" at the top of the clips panel), or
  tick "Save as a clip" in the Record or Insert recording dialog: the take
  becomes a clip ("Take 1", or the WAV file's name) instead of going into the
  song.
- **The clips panel** (`Source/ClipsPanel.cpp`), docked along the right of
  the main window, shows the song's clips at all times: a card for each, with
  its name, a picture of its notes (a colour per channel, drums as dots), its
  channels, length and linked copies, and a play button that plays it on its
  channels at the song's tempo. View > Clips panel shows or hides it (it
  comes back when FamiTracker starts if it was open), Edit > Clips
  (Ctrl+Shift+V, or the clips button next to the microphone) brings it up,
  and its left edge drags to make it wider.
- **Putting one in:** drag a card into the pattern editor. While dragging,
  cells it will cover are shaded, as many channels and rows as the clip has,
  and the cursor stays where it was; held near the editor's edge, the
  pattern scrolls a row or a channel at a time. Let go to put a copy in, or
  hold Ctrl as you let go for a linked copy; Esc stops the drag. Its first
  channel goes on the channel under the mouse, the rest to the right, from
  that row on, running into the following frames and adding frames at the
  end of the song when needed. What those channels had there is replaced;
  Ctrl+Z undoes it. Double-clicking a card puts a copy in at the cursor.
- **Into the piano roll:** a card dragged over the piano roll's notes shows
  the clip's notes where it would start, on the channel being edited and the
  ones to its right, snapped to the grid (hold Alt for any row). Held near
  the roll's left or right edge, the roll scrolls that way. Letting go puts
  it in there, as a copy or, with Ctrl, linked.
- **Right-click a card** for the rest: put it in at the cursor (as a copy or
  linked), listen, edit its notes, rename, duplicate, unlink its copies,
  delete, or save the selection as a new clip.
- **A copy** is plain notes. **A linked copy** stays the clip: edit any note
  of it in the song and the clip changes, and with it every other linked
  copy. A linked copy whose notes are all deleted stops being linked rather
  than emptying the others, and anything written over a linked copy (a take,
  chords, another clip) ends its link there. Unlink copies turns all of a
  clip's linked copies into plain notes.
- **Edit its notes...** opens the clip editor: the clip's notes in a list.
  Pick an instrument and give it to the selected notes or all of them, or
  transpose by a semitone or an octave (the selected notes, or all when none
  are selected). OK saves the clip, and its linked copies in the song
  follow, as one Ctrl+Z.
- **Duplicate...** makes a new clip from one: a copy to change on its own,
  leaving the original and its linked copies alone. Rename and Delete do
  what they say; deleting leaves its linked copies in the song as notes.

How linking works: a linked copy remembers its cells by pattern and row in
each channel (by channel ID), not by frame, so it keeps working when frames
are moved, inserted or removed. Placing a linked copy gives every frame it
touches a pattern of its own. After every edit (and undo and redo),
FamiTracker compares each linked copy with its clip; the one that changed
becomes the clip and the others follow. A link is dropped when its channel,
track or rows are gone (a shorter pattern length, a removed expansion chip).

The song's clips are saved in the module's `BF CLIPS` block, version 1:

- the number of clips; per clip its ID, name (NUL-terminated), channels,
  rows, the channel ID each column came from, and its cells a column at a
  time (note, octave, volume, instrument, then effect number and parameter
  per effect column);
- the next free ID; the number of linked copies; per copy its ID, clip ID,
  track and segments: channel ID, pattern, first pattern row, clip column,
  first clip row and row count.

Loading checks every count and value against FamiTracker's limits; a damaged
block leaves the clips out and the song still loads. Other FamiTracker
versions skip the block, and lose the clips if they save the song.

## Piano roll

View > Piano roll (Ctrl+Shift+P, or the toolbar button next to the clips
button) shows the song's notes under the pattern editor: time from left to
right, pitch from bottom to top, with a keyboard on the left. Drag the line
along its top to make it taller or shorter; the × in its top right corner,
or View > Piano roll again, hides it. It comes back when FamiTracker starts
if it was open when FamiTracker closed, and remembers its height and zoom.

Untick View > Pattern editor and the piano roll fills the pattern editor's
place; one of the two always shows, so hiding the piano roll then brings the
pattern editor back. In the pattern editor's place the piano roll takes its
keys: the note keys play notes and, with editing on (Space), type them at the
cursor line a step apart, the keypad's digits pick an instrument and F2-F9
an octave; the letters D, B, S and E are notes then, not the tools' keys.
Edit > Cut, Copy, Paste and Delete work on its selection.

Showing the song:

- The ruler counts bars from the second row highlight (a bar), with thinner
  lines for beats (the first highlight) and rows, and the strongest lines
  where a frame starts.
- Every channel's notes are drawn, each channel in its own colour: the one
  being edited (the pattern editor cursor's channel) bright, the others
  faint. A note lasts until the next note, release or halt in its channel,
  the way it plays. A note that slides into its pitch (`Qxy`, `Rxy`) sits at
  the pitch it lands on, with a tick where it starts.
- It follows the song: the play position runs across while playing (paging
  ahead in follow mode, the note sounding lights up, and so does its key),
  and the pattern editor's cursor shows as a line.
- Wheel: up and down; Shift+wheel: sideways; Ctrl+wheel: zoom in and out
  around the mouse. The arrow keys, Page Up/Down, Home and End scroll too.
  Hovering a note shows its name, length, instrument and bar in the status
  line along the bottom, which also says what the mouse can do.

The buttons along the top:

- The channel being edited (also: click a faint note, or move the pattern
  editor's cursor to another channel).
- Draw, Paint melody or Select (keys D, B and S).
- Grid: what new notes snap to: a row, half a beat, a beat, two beats or a
  bar. Hold Alt for single rows.
- Key: none, one picked from a list (major, minor and their pentatonic
  scales), or "Find the key from the song's notes". With a key, lanes
  outside it are darker, its notes are marked on the keyboard, new and moved
  notes stay in it (Alt lets them out), notes outside it get a red outline,
  and the arrow keys move notes by notes of the key.
- Instrument: the one new notes get, the same as the instrument list
  (clicking a note picks its instrument there too). "Give the selected
  notes" changes theirs. An instrument for another chip is skipped for the
  instrument the channel already plays.
- Effects: effects for the selected notes (see Effects below).
- Chords: a chord on the channels that are free there (see Chords below).
- Groove: drums, a bass line and chords for a mood (see Mood starters
  below).
- Clips: place a clip with the mouse (as a copy, or linked when "Place
  linked copies" is ticked), save the selected notes as a clip
  (Ctrl+Shift+C), or show the clips panel.

On a narrow window the buttons go on two rows, or more, so none of them is
lost; when the piano roll is too short for those rows and its notes, the
buttons that don't fit go under More, at the end of the last row (a taller
piano roll shows them again). A click that only closes a menu (a button's, or any other) does
nothing else in the piano roll, and after a dialog or a message box the keys
come back to the piano roll if it had them. Ctrl+Z and Ctrl+Y there keep it
on its channel, with its selection, whatever channels the change was on.

Editing, in the bright channel:

- Draw: click to add a note a grid step long, or drag to make it longer;
  dragging up and down changes its pitch while drawing. It ends with a
  release (`===`) when its instrument fades out on one, otherwise a halt
  (`---`). Clicking another channel's note edits that channel instead;
  hold Shift to draw over it.
- Paint melody: drag, and a note goes on every grid step at the pitch under
  the mouse; steps of the same pitch join into a longer note. A way to sketch
  a melody's shape without singing in tune, kept in key when a key is set.
- Drag a note to move it (with any other selected notes); Shift+drag copies
  them. Drag its right end to make it longer or shorter: it can't run past
  the next note.
- Right-click erases a note; right-drag erases every note it passes.
- Click selects a note, Ctrl+click adds or removes one, Ctrl+drag selects
  the notes in a box, Ctrl+A selects all of them, Esc none. With the Select
  tool, a plain drag selects the notes in a box, dragging along the ruler
  selects the notes that start in those rows (whole bars, say), and a click
  on an empty spot selects none.
- The selected notes become a clip with Ctrl+Shift+C, the Clips button, or
  Edit > Save selection as clip and the clips panel's "+ Save selection"
  (which take the piano roll's notes when the pattern editor has no
  selection: a selection made in one ends the other's).
- With notes selected: Delete erases them; Up and Down move them a semitone
  (a note of the key, with a key), Shift+Up and Shift+Down an octave; Left and
  Right a grid step (Alt: a row); Ctrl+C, Ctrl+X and Ctrl+V copy, cut and
  paste them, at the mouse (or the pattern editor's cursor).
- A note put where others are replaces them: notes starting under it make
  way, and one sounding into it ends where it starts.
- Notes can go one frame past the end of the song; the song gets the frame,
  and a note that sounded on to the old end still ends there.
- Ctrl+Z undoes each change (one drag, one key) and Ctrl+Y redoes it.

Effects:

- The Effects button, the E key (while the pattern editor shows; in its
  place E is a note) or a double-click on a note opens a menu of effects for
  the selected notes (E and the double-click select the note under the
  mouse when none is). Each item shows the effect it writes, so
  the pattern editor's effect column becomes familiar:
  - Vibrato, gentle (`452`), medium (`454`) or strong (`467`), starting just
    after the note does.
  - Tremolo, the volume wobbling: gentle (`753`) or strong (`777`).
  - Chord, as an arpeggio (`0xy`): major, minor, a power chord, an octave,
    sus4, sus2, diminished, augmented, or with a key picked the key's own
    chord on each note.
  - Slide into the note from a semitone, 2, 3 or 5 below, an octave below,
    or 1 or 2 above (`Qxy`, `Rxy`); the note still lands where it was.
  - Glide from the note before, quick, medium or slow (portamento, `3xx`).
  - Bend at the end: down a semitone or two, a fall of an octave, or up a
    semitone, half a beat before the note ends.
  - Fade out over the note's second half, quick, medium or slow (`Axy`); the
    next note gets its volume back.
  - Remove all effects (but the song-wide ones).

  Each group also has a "No ..." item to take that effect off. Drums get
  only tremolo and fades, the triangle no volume effects, and DPCM none.
- The effect goes on the notes chosen only: the notes after them sound as
  before (a vibrato or chord they would take over is switched off on their
  first row, or set again there). When an effect column is full, the menu's
  message says so: add a column with the small arrow at the channel's name
  in the pattern editor.
- Effects are drawn on the notes: vibrato as a wave, tremolo dotted along the
  top, a chord's other notes as thin bars in their lanes, a glide as a line
  from the note before, a bend as a line to where it goes, a fade as a
  wedge, other effects as a small mark on their row. Hovering a note lists
  its effects in words, including ones it takes over from an earlier note.
- Ctrl+Z undoes each one.

Chords:

- The Chords button, the H key (while the pattern editor shows; in its
  place H is a note) or a right-click on the chord strip under the ruler
  puts a chord on the channels that play nothing in its bars: a note on
  each, held through the bar, close together under the melody and over the
  bass, so the chord fills the space between them.
- The chord: one of the key's chords (with sevenths in a submenu), the ones
  the strip names there, or one typed: a name like `Am`, `G7`, `Fmaj7` or
  `C/E`, a degree of the key like `vi`, or a chord a bar like `C G Am F`
  (`| Am | F G |` puts two in the second bar).
- Where: the bar the cursor is in (chords typed for more bars go on over
  the bars after it), or the bars of a selection: rows dragged along the
  ruler with the Select tool, the selected notes, or the pattern editor's
  selection. Chords for fewer bars than the selection repeat over it.
- Free channels are those with no note sounding in those bars that play a
  chord's note at a volume of their own: not the triangle, the noise or the
  DPCM. The VRC7's channels take the voices first, then the soft layer
  (N163), the 5B's, the pulses, the FDS and last the sawtooth, all on one
  kind of channel where enough of it is free.
- Each voice gets a quiet sound for its chip, made the first time: Soft
  chords on the pulses, the 5B and the sawtooth, Reed organ on the VRC7,
  Warm organ on the FDS, and on the VRC6 Plus's soft layer a String pad of
  its own for each voice ("String pad 2", "String pad 3"...), since two
  N163 channels can't share an instrument.
- Voices sets how many voices a chord gets: one for each of its notes (3,
  or 4 with a seventh), or 2, 3 or 4. With fewer free channels than that, a
  menu says so and offers as many voices as there are free channels, one
  on each, or the chord in turn on one channel as an arpeggio (`0xy`, which
  plays three notes).
- Again on the next bar puts the last bar's chords on the bar after the
  ones written, on the same channels, led on from them (while those chords
  are still there: not after Ctrl+Z took them back).
- The status line says what went where, and when the melody rubs a
  semitone against a chord's note. One Ctrl+Z takes the whole chord back
  (the instruments it made stay in the song).

Mood starters:

- The Groove button, or in an empty song the "Start from a groove" box in
  the middle of the notes, opens a menu of moods: town, overworld, battle,
  boss, dungeon, sad, calm, chase, upbeat, shop, underwater, castle, desert
  and nostalgic, each with what it's for. Pick one, and its drums, bass line
  and chords come in, played the way that mood is usually arranged: the
  same groove as Edit > Start from a groove (see Recording in FamiTracker).
- Where: under the bars of a selection (rows dragged along the ruler with
  the Select tool, the selected notes' bars, or the pattern editor's
  selection), or from the bar the cursor is in for the length set in the
  menu: 4, 8 or 16 bars.
- The melody stays on the channel being edited (a pulse when that's the
  bass or the drums); the chords go on another pulse, the bass on the
  sawtooth or the triangle, the drums on the noise, and what those channels
  played in those bars makes way. The menu says which channels.
- The tempo: an empty song gets the mood's (each mood shows it); a song with
  notes keeps its own, and the status line says when the mood suits another.
  The key keeps to the notes the song already has, so a melody there fits;
  an empty song starts on the white keys (C major, or A minor for the darker
  moods).
- More choices opens the groove dialog for another tempo (typed or tapped),
  key or length. One Ctrl+Z takes the groove back, with the tempo and the
  key it set (the instruments it made stay in the song).

How edits keep the song sounding right:

- A note takes its cells with it: its instrument and volume, the effects on
  its rows, and its release or halt. Effects that steer the whole song (`Fxx`,
  `Bxx`, `Dxx`, `Cxx`) stay on their rows.
- Notes an edit doesn't touch keep sounding the same: when the volume or
  instrument they took over from an earlier note would change, it is written
  into their own cell. Moved and pasted notes keep how they sounded too.
- Effects that switch something off (`000`, `400`, `700`, `A00`, `100`,
  `200`, `300`) go where a note now ends when it is cut short or removed, so
  a vibrato or arpeggio doesn't run on.
- A frame that plays the same pattern as another frame gets a pattern of its
  own before its notes change, so the other frame stays as it was. A note
  inside a linked copy of a clip is edited there, and the clip and its other
  copies follow.
- The cells are written with the same undoable action as recordings
  (`WriteChannelRows` in `InsertRecording.cpp`); `NoteEdit.cpp` works them
  out and is tested on Linux (`Source/Tests/NoteEditTests.cpp`).

## Using wav2ftm today

```
wav2ftm --print whistle.wav whistle.txt                 melody on VRC6 pulse 1
wav2ftm --bpm 120 taps.wav taps.txt                     taps are recognised: drums
wav2ftm --mode hum humming.wav melody.txt               say what it is instead
wav2ftm --key C --scale pentatonic hum.wav hum.txt      snap to a scale
wav2ftm --channel 7 bassline.wav bass.txt               VRC6 sawtooth, bass register
wav2ftm --min-rest 0 staccato.wav staccato.txt          keep every break as a rest
wav2ftm --plain whistle.wav plain.txt                   no loudness, slides or vibrato
wav2ftm --backing whistle.wav song.txt                  add chords and a bass line
wav2ftm --backing --key A --scale minor hum.wav song.txt   ...in A minor
wav2ftm --drums taps.wav --bass hum.wav whistle.wav song.txt   all three in one song
```

In a song from several recordings, each part starts at row 0 with its first
sound, so start every recording on the first beat. With `--keep-silence`,
each part keeps its timing from the start of its file instead, which lines
the parts up if every recording starts at the same moment. The bass goes on
the VRC6 sawtooth (the triangle with `--expansion none`), an octave or so
below the melody, as instrument 04.

Then, in FamiTracker: File > Import Text. Convert phone recordings in other
formats to WAV first, e.g. `ffmpeg -i memo.m4a memo.wav`.

## How it works

- **What a recording holds** (`GuessSource`, `--mode auto`): taps and
  beatboxing are pitched less than half the time they sound; whistling and
  humming nearly all the time. A whistle is an almost pure tone (under 10% of
  its energy in overtones 2-5, measured with Goertzel filters), a voice is rich
  in overtones; very high melodies (median from 900 Hz) count as whistling.
  Whistling is then tracked at 300-4200 Hz and a voice at 70-1100 Hz, which
  keeps out octave mistakes. `--mode melody` keeps the full 70-4200 Hz.
- **Melody:**
  1. `TrackPitch` runs YIN with a 20 ms window and a 5 ms hop.
  2. `SegmentNotes` treats frames more than 35 dB below the loudest frame as
     silence. It starts a new note when the pitch moves at least 0.6
     semitones for at least 35 ms. Each note then takes back the quieter
     start and end of its own tone: neighbouring frames that are still
     pitched, within a semitone, no more than 30 dB below the note's peak and
     6 dB above the noise floor (the 10th percentile of frame levels). The
     loudest moment of a recording sets the gate for everything, so without
     this, quieter notes lose their fade-out.

     Singing and humming also split at syllables (`NoteOptions::syllables`).
     The consonant between two syllables dips the loudness, so a dip of at
     least 6 dB below the loudest moment within 150 ms on both sides starts a
     new note, even when the pitch carries on. A stretch of 15 ms or more
     without a pitch (a consonant or a breath) also ends a syllable. Inside
     a syllable, a held note is at least 120 ms on one pitch; shorter pieces
     and glides (moving 1.5 semitones or more from start to end) join the
     held note next to them: before it they are a scoop, kept as a slide
     into the note, after it a drift. A second held pitch in the same
     syllable ("la-a") stays a note of its own.
  3. `OctaveShiftFor` moves the median note to G4 (MIDI 67) for leads, or to
     C3 (MIDI 48) for the triangle and sawtooth, which usually play bass.
  4. `SnapToScale`, then `Quantize`. A note starts on its nearest row and
     keeps its rounded duration, so a 180 ms note is 2 rows wherever it
     starts. A break shorter than `minRestSeconds` (150 ms) is held through,
     notes that touch always join up, and two notes that land on one row
     merge into one note at the longer one's pitch, unless the second starts
     a sung syllable: that one moves to the next row if it is free. Slides
     are only folded in within a syllable.
- **Pitch names:** FamiTracker's note index is MIDI − 24, so `A-3` is 440 Hz.
- **Rows per second:** `F * floor(24T/S) / (60F - (24T mod S))`, the same
  formula as `CSoundGen`'s tempo accumulator.
- **Hits:**
  - Energy is measured in 10 ms frames. A hit is a rise of at least 9 dB over
    the quietest frame of the previous 50 ms.
  - Brightness is the share of first-difference energy in the first 30 ms.
  - A 1D k-means on log-brightness gives one to three groups.
- **Text output:**
  - Same layout as `CTextExport::ExportFile`.
  - Titles are cut to 31 characters, because FamiTracker copies them with
    `strcpy_s` into 32 bytes.
  - `ValidateTextModule` runs on every write.
  - The Lead instrument's volume macro is `11 14 15 15 14 13 12 11 10 7 4 2 0`
    with the release point on item 8: it holds at 10 while the note lasts,
    and the `===` at the note's end plays `7 4 2 0`. Melodies on the triangle
    or sawtooth use the Bass envelope `15 14 13 12 11 10 7 4 2 0` (release
    point 5, volume macro 4). On the VRC6 the lead's duty macro loops
    `3 3 3 3 3 3 2 2 2 2 2 2`.
  - Expression goes into the volume column and the first effect column.
- **Takes recorded in FamiTracker** (`Take.cpp`):
  - The click is a decaying tone (1.5 ms rise, 7 ms decay, cut at 40 ms):
    1046.5 Hz (C6) on the first beat of a bar, 784 Hz (G5) on the others.
  - The click and the microphone are separate Windows streams (`waveOut`,
    `waveIn`). While the click plays, the two sample counts are read together
    every 10 ms; their median difference says where each click falls in the
    take.
  - What the counts don't show (the sound card, the air, the player) is
    measured when possible. On speakers, `FindClickLeak` finds the count-in
    clicks in the take: hits within 120 ms of where they should be, mostly a
    tone at the click's pitch (over 0.3 of the energy at that frequency, by
    Goertzel; a tap scores a few hundredths), agreeing on the delay within
    12 ms. Otherwise the delay from Test timing is used (`MeasureTapDelay`:
    the median of eight taps, when at least six are found and the middle half
    agree within 60 ms), or 30 ms.
  - `PlaceNotes` and `PlaceHits` make the downbeat time 0 and drop what came
    before it (starting more than a row early for a note, half a row for a
    hit). Then they settle the take: the mean phase of the onsets within a row,
    weighted by note length or hit strength, says how late it sits; when the
    onsets agree, everything moves back by that much. From 0.35 rows early to
    half a row late counts: a take is more often late than early.
  - `RemoveClickHits` drops hits within 30 ms of a click that sound like the
    click and aren't twice as loud as the count-in clicks were.
- **Chords and bass** (`Harmony.cpp`):
  - The key: the melody's pitch classes, weighted by length and loudness,
    against Krumhansl and Kessler's major and minor key profiles; the last
    note (and a little, the first) counts towards the key it is the tonic of.
  - The chords of a key: I ii iii IV V vi in major, i iv v V VI III VII in
    minor (the major V for the raised seventh).
  - Each bar, or half bar, scores every chord: melody notes that are chord
    tones count for it, other notes of the key a little against it, notes
    outside the key more, and a long note a semitone above a chord tone the
    most. Notes on the first beat count 1.5 times, on other beats 1.2 times,
    quick passing notes 0.6 times.
  - A Viterbi pass picks the best path: changing chord costs 0.4 (0.6 more
    halfway through a bar), and steps that lead somewhere get some of that
    back (away from home to IV or ii, then to V, then home: V-I most), so
    with no melody the chord just stays. It starts and ends on the tonic
    when the melody allows.
  - The bass plays the root on every chord and bar, and the fifth halfway
    through bars of four beats or more, in C2-G3, moving as little as it can.
    The arpeggios pick root position or an inversion (`047`, `038`, `059` for
    major chords) whose lowest note is nearest the last chord's, in F3-F4,
    struck on every chord and bar; a release with `000` ends them.
- **Tapping the tempo** (`Tapping.cpp`): the taps count from the last pause
  (two seconds, or two and a half beats); a tap far too soon after another
  is a double tap and a gap of about two beats a beat missed. The beat is a
  least-squares line through the taps, each on the beat nearest it. Speed
  and tempo: `ftapi::TappedSpeedTempo` (`Api/RecorderInput.cpp`).
- **Strummed chords** (`Strums.cpp`): a strum starts where the spectral flux
  (the growth of the log spectrum, 10 ms frames, 30 Hz-8 kHz) peaks well
  over its surroundings in sound loud enough, at the start of its rise,
  since the strings come one after another; strums under 100 ms apart are
  one. Its notes come from the spectrum just after the pick's scrape, until
  the next strum (up to 0.6 s), on a scale of thirds of a semitone: every
  note from C2 to E6 as a set of harmonics, softer the higher they are and a
  little sharp as strings are, fitted to what stands out of the spectrum
  without going negative (multiplicative updates with a little push towards
  fewer notes), so overtones aren't notes of their own. Up to six notes
  count, from 15% of the strongest. The delay through the take comes out
  first: where most strums agree they lie past the beats (up to three
  quarters of a beat late or a quarter early, within a sixth of a beat of
  one another), which a microphone delay not measured puts on every strum;
  then a strum within a third of a beat of a beat counts from the beat
  (strummed a little early or late). Each bar's (or half bar's) chord is
  then named from the notes sounding in it, the lowest a strum's bass, as
  the tools hear chords in a song's parts (`HearChords` in
  `Api/Chords.cpp`).
- **Typed chords** (`Api/RecorderInput.cpp`): read with the tools' chord
  names (`Chords.cpp`) and laid on the bars from the first one on, repeating
  over the melody's bars when it has more; the key is the one their notes
  fit, the first chord's root the likeliest tonic. A chord with more notes
  keeps them (`transcribe::Chord::tones`): the arpeggio plays its root, its
  third (or suspension) and its most telling other note, spread out rather
  than a step apart, and the bass plays a slash chord's bass note.
- **Speed:** YIN's difference function comes from an FFT cross-correlation
  plus running energy sums, giving the same values as the direct sum. A
  34-second recording takes about half a second, where the direct sum took
  11 seconds.

## Building and testing the engine

```
cmake -S Source/Transcribe -B build/transcribe
cmake --build build/transcribe
ctest --test-dir build/transcribe --output-on-failure
```

## Limits

- One note at a time, which suits NES channels.
- Taps all sound alike unless struck differently, for example finger pad vs.
  nail. Beatboxing gives clearer kick, snare and hi-hat differences.
- WAV files have no click, so their first note lands on the cursor row and a
  steady tempo helps. Takes recorded in FamiTracker follow the click instead.
  A WAV file of strummed chords has to be played at the song's tempo, its
  first strum on the first bar of the chords.
- Strummed chords are heard best strummed cleanly, a chord or two a bar;
  ringing open strings and fast changes can blur a bar's chord, which you
  can fix by name before pressing Add.
- Timing on speakers relies on the microphone hearing the count-in. With
  headphones, Test timing measures the delay once; Bluetooth headphones can
  be slow enough that this is needed.

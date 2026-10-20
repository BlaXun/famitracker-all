# Recorder: whistle, hum or tap, and get notes

Branch `recorder`, made from `beginner-friendly`. Record a melody (whistle,
hum, sing) or a rhythm (finger taps, beatbox) and get FamiTracker notes. Later,
recordings and selections become reusable clips.

## Requirements

- **A note lasts as long as the sound that made it.** A long hum becomes a long
  note. The lead instrument holds its volume until a note release (`===`) where
  the sound stops, then fades out over about 80 ms instead of stopping dead.
  Breaks shorter than 150 ms between two notes are held through, the way a
  musician would write them. `TestNoteLengths`, `TestQuietEdges` and
  `TestQuantizeRules` cover this.
- Output stays plain FamiTracker pattern data. There are no new `.ftm` blocks,
  so songs open in any FamiTracker.

## Status

- [x] Detection engine in `Source/Transcribe/`: plain C++14 with no
  Windows/MFC dependencies. It is also compiled into `FamiTracker.vcxproj`,
  where Insert recording and Record from microphone use it.
  - WAV input (PCM 8/16/24/32-bit, float; stereo is downmixed).
  - YIN pitch tracking from 70 Hz to 4.2 kHz, covering a low hum up to a high
    whistle.
  - Note segmentation. It bridges dropouts of up to 40 ms and keeps vibrato and
    short glides inside one note.
  - Octave fit, scale snap, and row quantization using FamiTracker's own tempo
    math.
  - Tap detection, sorting hits into kick, snare and hi-hat by brightness.
  - A FamiTracker text-module writer, plus a validator ported from
    `CTextExport::ImportFile`.
- [x] `wav2ftm` command-line tool. CI builds it as the `wav2ftm-Win32`
  artifact. Import Text always replaces the open song, so `wav2ftm` combines
  a melody, a drum part and a bass line from separate recordings into one
  song (`--drums`, `--bass`).
- [x] Tests: 323 checks on synthesized audio covering:
  - pitch of hums and whistles, and telling them and taps apart,
  - note lengths, quiet note endings, rounding to rows and short breaks,
  - repeated notes and glides, slides, loudness and vibrato,
  - octave fit and scales,
  - taps,
  - the exact text cells, including songs with several parts.
  - the FFT difference function against the direct sum,
  - the click, finding it in a take recorded on speakers and taking it out
    of drum hits, measuring the delay of taps, and lining takes up with rows.

  Also checked by hand on noisy recordings with room echo, pitch scoops and
  timing wobble.
- [ ] Tuning on real recordings. Round one, a 34-second whistle: notes sounded
  cut short. Quiet note endings were lost under the silence gate, rounding
  start and end separately cost up to 80 ms per note, short breaks became
  hard cuts, and two notes on one row lost the second one's length. All four
  are fixed. Before, 11 of 40 notes in that recording stopped more than 50 ms
  before the whistle did; now, counting the release fade, none do.
- [x] Edit > Insert recording (WAV) in FamiTracker, also in the pattern
  editor's right-click menu (`Source/InsertRecording.cpp`). A dialog
  (`InsertRecordingDlg.cpp`) asks what the recording holds, preselected with
  FamiTracker's guess (whistling, humming or singing, taps or beatboxing), and
  which channel it goes on: the cursor's channel for a melody, the noise
  channel for drums. It writes the notes into the open song from the cursor
  row on. It uses the song's
  speed and tempo, and runs on into the following frames, adding frames at
  the end of the song if needed. A touched frame whose pattern other frames
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
    highlight, a bar the second), then clicks along. It can also count in
    only, or not click at all.
  - "Play the song along" plays the song from the cursor while you record,
    starting so its first row sounds with the downbeat. The channel being
    recorded stays quiet, so an earlier take doesn't play along.
  - Stop puts the take in at the cursor row, the downbeat after the count-in
    on that row, as one undoable action, and saves it as a WAV file in
    Documents\FamiTracker recordings.
  - Test timing: tap along to eight clicks, and takes are lined up with how
    late the taps arrived (see How it works).
  - "From a WAV file..." switches to Insert recording.
- [ ] Harmony backing, next: find the melody's key, pick chords that fit it bar
  by bar, and write a bass line on the sawtooth and the chords as fast
  arpeggios on VRC6 Pulse 2. Takes recorded against the click give the
  chords a beat to land on.
- [ ] AI arranging over MCP, after that: the `api` branch (ROADMAP.md) lets an
  MCP server read and edit the open song, so an assistant can arrange it.
- [ ] Clips:
  - save a recording or a pattern selection as a clip,
  - place clips with the mouse, either as a copy or linked,
  - keep the library next to the song.
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

Use headphones, so the click and the song stay out of the take. On speakers,
FamiTracker finds the click in the take and lines the notes up with it, and
takes the clicks out of drum takes, but a song playing along can still end up
in a melody. If the notes come out late, press Test timing once and tap
along. If the meter says "No sound?", check that Windows' privacy settings let
apps use the microphone.

## Using wav2ftm today

```
wav2ftm --print whistle.wav whistle.txt                 melody on VRC6 pulse 1
wav2ftm --bpm 120 taps.wav taps.txt                     taps are recognised: drums
wav2ftm --mode hum humming.wav melody.txt               say what it is instead
wav2ftm --key C --scale pentatonic hum.wav hum.txt      snap to a scale
wav2ftm --channel 7 bassline.wav bass.txt               VRC6 sawtooth, bass register
wav2ftm --min-rest 0 staccato.wav staccato.txt          keep every break as a rest
wav2ftm --plain whistle.wav plain.txt                   no loudness, slides or vibrato
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
  3. `OctaveShiftFor` moves the median note to G4 (MIDI 67) for leads, or to
     C3 (MIDI 48) for the triangle and sawtooth, which usually play bass.
  4. `SnapToScale`, then `Quantize`. A note starts on its nearest row and
     keeps its rounded duration, so a 180 ms note is 2 rows wherever it
     starts. A break shorter than `minRestSeconds` (150 ms) is held through,
     notes that touch always join up, and two notes that land on one row
     merge into one note at the longer one's pitch.
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
  - The click is a 40 ms decaying tone: 1760 Hz on the first beat of a bar,
    1320 Hz on the others.
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
- Timing on speakers relies on the microphone hearing the count-in. With
  headphones, Test timing measures the delay once; Bluetooth headphones can
  be slow enough that this is needed.

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
  Windows/MFC dependencies. It is also compiled into `FamiTracker.vcxproj`, but
  the app does not call it yet.
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
  artifact.
- [x] Tests: 174 checks on synthesized audio covering:
  - pitch of hums and whistles,
  - note lengths, quiet note endings, rounding to rows and short breaks,
  - repeated notes and glides,
  - octave fit and scales,
  - taps,
  - the exact text cells.

  Also checked by hand on noisy recordings with room echo, pitch scoops and
  timing wobble.
- [ ] Tuning on real recordings. Round one, a 34-second whistle: notes sounded
  cut short. Quiet note endings were lost under the silence gate, rounding
  start and end separately cost up to 80 ms per note, short breaks became
  hard cuts, and two notes on one row lost the second one's length. All four
  are fixed. Before, 11 of 40 notes in that recording stopped more than 50 ms
  before the whistle did; now, counting the release fade, none do.
- [ ] Record dialog in FamiTracker:
  - microphone input via `waveIn` (winmm),
  - count-in and metronome at the song's tempo,
  - preview,
  - insert at the cursor through `CPatternAction` so it can be undone.
- [ ] Clips:
  - save a recording or a pattern selection as a clip,
  - place clips with the mouse, either as a copy or linked,
  - keep the library next to the song.
- [ ] Turn a recorded sound into a DPCM sample with the existing WAV import.

## Using wav2ftm today

```
wav2ftm --print whistle.wav whistle.txt                 melody on VRC6 pulse 1
wav2ftm --mode drums --bpm 120 taps.wav taps.txt        rhythm on the noise channel
wav2ftm --key C --scale pentatonic hum.wav hum.txt      snap to a scale
wav2ftm --channel 7 bassline.wav bass.txt               VRC6 sawtooth, bass register
wav2ftm --min-rest 0 staccato.wav staccato.txt          keep every break as a rest
```

Then, in FamiTracker: File > Import Text. Convert phone recordings in other
formats to WAV first, e.g. `ffmpeg -i memo.m4a memo.wav`.

## How it works

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
     G3 (MIDI 55) for the triangle and sawtooth.
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
  - The Lead instrument's volume macro is `15 14 13 12 11 10 7 4 2 0` with
    the release point on item 5: it holds at 10 while the note lasts, and the
    `===` at the note's end plays `7 4 2 0`.
- **Speed:** about 4x faster than real time, since YIN costs window × lag
  operations per frame. An FFT-based difference function would speed this up
  if needed.

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
- A steady tempo helps. A count-in and metronome come with the record dialog.

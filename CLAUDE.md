# CLAUDE.md

Guidance for working in this repository. This is the `recorder` branch, made
from `beginner-friendly`: FamiTracker 0.4.6 with a modernized build, plus the
audio-to-notes recorder described in `RECORDER.md`: the engine in
`Source/Transcribe/`, the `wav2ftm` tool, Edit > Insert recording in
`Source/InsertRecording.cpp`, and Edit > Record from microphone in
`Source/RecordDlg.cpp` (audio I/O in `Source/MicRecorder.cpp`). The overall
plan is in `ROADMAP.md`. Unless a section says otherwise, everything below
describes the 0.4.6 code as found on `master` and was checked against that
source.

## What this repository is

An archive, not an ordinary project. It holds every public release of the
official FamiTracker source (by Jonathan "jsr" Liss, GPLv2+), imported
verbatim, one commit per release. FamiTracker is a Windows/MFC music tracker for
the NES/Famicom 2A03 sound chip and the VRC6, VRC7, FDS, MMC5 and N163 expansion
chips, with NSF/NES export. This repo is a fork of `HertzDevil/famitracker-all`.

- The `master` tree is FamiTracker 0.4.6 (Feb 2015, `config.h` SVN_VERSION
  369) plus `README.md` and this file. Commits after `5fb684d` touch only
  those two files.
- There are no tags; each release is identified by its commit subject. Commit
  dates are 2016 import dates, and the real release date is in the subject.
- The archive stops at 0.4.6. `changelog.txt` lists 0.4.5, but no 0.4.5 source
  exists here.
- Never rewrite, rebase, squash or force-push the release commits. Keep
  `master` a faithful archive plus docs, and do code work on a branch made from
  `master` or from the release being studied.

## Releases on `master`

| Release | Commit | Layout at that point |
|---|---|---|
| 0.2.2 | `f0a8e52` | Flat root; APU emulation in `player/`; VS2003 project |
| 0.2.4, 0.2.5 | `f40e5fd`, `d21d7d0` | `Source/` (.cpp), `Include/` (.h), `sound driver/` (SoundGen, APU); `FFT/` at root from 0.2.5 |
| 0.2.6, 0.2.7, 0.2.9, 0.3.0 | `6a62a32`, `28fe1a2`, `a7cce05`, `41cb122` | `Source/`, `Source/APU/`, headers in `Include/`, `FFT/` at root; VS2008 project from 0.2.9 |
| 0.3.5, 0.3.6, 0.3.7 | `42fdd43`, `17a64a7`, `d7928f0` | Headers move into `Source/`; `Source/drivers/`, `Source/Blip_Buffer/` |
| 0.4.0, 0.4.1, 0.4.2, 0.4.3, 0.4.4, 0.4.6 | `2063ad9`, `8bbe96c`, `8913aaf`, `01ce364`, `95a43e5`, `5fb684d` | `Source/FFT/`; `version.h`, `config.h`, `changelog.txt` at root |

Files moved between these layouts. `git diff` between two releases detects
most moves as renames (64 between 0.2.9 and 0.3.5). To compare one file across
a move, name both blobs:
`git diff a7cce05:Include/FamiTrackerDoc.h 42fdd43:Source/FamiTrackerDoc.h`.

## Community fork branches

Upstream has nine more branches, each a single commit on top of an official
release. If they are missing here, run
`git remote add upstream https://github.com/HertzDevil/famitracker-all && git fetch upstream`.
Then `git diff upstream/<branch>^ upstream/<branch>` shows exactly what a mod
changes.

| Branch | Based on | Contents |
|---|---|---|
| `nsf-importer` | 0.3.6 | NSF Importer 0.1 by Brad Smith: "Import NSF..." using the NotSo Fatso NSF core (`Source/NSF_Import/`); pattern editing deliberately disabled |
| `sweep-hack` | 0.4.2 | Sweep Macro Hack Edition: hardware-sweep macro for 2A03 instruments |
| `slimeball` | 0.4.2 | One-line change forcing linear N163 mixing (`SetNamcoMixing(TRUE)`) |
| `ipi` | 0.4.2 | ipi's edit: `Lxx` delayed release, reworked `Exx`, multiple expansion chips |
| `uber` | 0.4.2 | ÜberTracker: multiple expansion chips via checkboxes, Sunsoft 5B enabled |
| `arp-scheme` | 0.4.2 | Arpeggio scheme sequence mode; ships rebuilt NSF driver headers |
| `fds-seq` | 0.4.6 | FDS instruments with regular sequences and up to 16 waves; rebuilt FDS driver |
| `lc` | 0.4.6 | Localization engine (`lang/`); VS2015 `.vcxproj` |
| `sneven` | 0.4.6 | SnevenTracker test build: SN76489 replaces the 2A03, expansion chips removed; NSF export still uses the 2A03 driver; `.vcxproj` mixing the `v100` and `v140` toolsets |

HertzDevil's own forks (`arp-scheme`, `fds-seq`, `sneven`) tag most changed
blocks with `// // //`, which makes their changes easy to find. Not every hunk
carries the marker, so `git diff` remains the complete list.

## Building

Windows only; nothing builds or runs in a Linux container. This branch
replaced the VS2008 project:

- Open `FamiTracker.sln` in Visual Studio 2022 or later with the "Desktop
  development with C++" workload and the C++ MFC component. No environment
  variables or extra SDKs are needed. From a command line,
  `msbuild FamiTracker.sln /p:Configuration=Release /p:Platform=x86` writes
  `build\Win32\Release\FamiTracker.exe`.
- `FamiTracker.vcxproj` was generated from the old `FamiTracker.vcproj` (gone
  on this branch, still on `master`) with the same file list. When you add a
  source file, add it to both `FamiTracker.vcxproj` and
  `FamiTracker.vcxproj.filters`.
- Project settings:
  - Toolset `$(DefaultPlatformToolset)`, i.e. whatever the installed Visual
    Studio provides.
  - Static MFC, Multi-Byte character set, C++14.
  - Precompiled headers off.
  - Conformance mode off, since the code relies on Microsoft extensions.
  - SDL checks off.
  - `GenerateManifest` off, because `FamiTracker.rc` already embeds
    `res/FamiTracker.manifest`.
- Link libraries: `Dbghelp winmm comctl32 dsound dxguid Version`, all from the
  Windows SDK. Boost and the DirectX SDK are no longer needed.
- **CI:** `.github/workflows/build.yml` builds Win32 Release and Debug on
  `windows-latest` and uploads `FamiTracker.exe` as an artifact. In October
  2026 that runner had Visual Studio 2026 (MSVC 14.51).
  - Each configuration also gets a check run, "Build report (<config> Win32)",
    with every compiler and linker diagnostic, the toolchain, the exe's DLL
    dependencies and its embedded manifest.
  - From a Linux session CI is the only way to compile. Push, wait for
    `gh run list --branch <branch>`, then read the report with
    `gh api repos/BlaXun/famitracker-all/commits/<sha>/check-runs --jq '.check_runs[] | select(.name | startswith("Build report")) | .output.title, .output.text'`.
  - Job logs and artifact downloads can be blocked in sandboxed sessions; the
    report doesn't need them.
  - After a green build of `beginner-friendly` or `recorder`, the `publish` job
    replaces that branch's rolling GitHub release (tag `build-<branch>`)
    using `.github/scripts/publish-build.sh`. `beginner-friendly` is marked
    Latest and `recorder` is a pre-release. Each release has a zip with
    `FamiTracker.exe` (plus `wav2ftm.exe` on recorder) and a debug-symbols
    zip. These tags are build pointers and get deleted and recreated; never
    use them for real releases.
- The baseline is 0 errors and 0 warnings at `/W3` in both configurations.
  Keep it that way so new warnings stand out.
- On `master`, the VS2008 project needs the `DXSDK_DIR` and `BOOST_DIR`
  environment variables. There, `Debug|x64` and `Release|x64` point the linker
  at `Lib\x86`; only the two `Release 64` configurations use `Lib\x64`.
- Compile-time switches:
  - `TRANSPOSE_FDS` (defined in `FamiTrackerDoc.h`) and `SUPPORT_TRANSLATIONS`
    (defined in `FamiTracker.h`; loads `language.dll` if its version matches)
    are on.
  - `EXPORT_TEST` is never defined and needs an external `ExportTest.dll`.
  - `ENABLE_CRASH_HANDLER` is commented out in `Source/exception.h`.
  - `RELEASE_BUILD` marks official builds: it hides on-screen build labels,
    drops the `-custom` crash-dump suffix and forces `EXPORT_TEST` off.
  - `AUTOSAVE` and `WIP` are off.
- The 6502 NSF driver is not built here; see NSF export below.

## Testing and verification

There are no automated tests. What exists:

- **CI build:** every push compiles Win32 Release and Debug (see Building).
- **Transcription engine** (`Source/Transcribe/`): plain C++14 that builds and
  tests on Linux with `cmake -S Source/Transcribe -B build/transcribe`,
  `cmake --build build/transcribe` and
  `ctest --test-dir build/transcribe --output-on-failure`. CI runs these tests on
  Linux and with MSVC, and lists the result in the Release build report.
- **Headless export:** `FamiTracker song.ftm -export out.<ext> [log.txt] [dpcm.bin]`.
  The module path must come before `-export`. The output extension picks the
  format: `.nsf`, `.nes`, `.bin` (plus the DPCM file), `.prg`, `.asm` or
  `.txt`. Any other extension goes to the first custom exporter plugin,
  whatever extension that plugin handles, and that path passes
  `CFamiTrackerDoc::GetDoc()`, which needs the main window; a command-line
  export runs before the window exists. Exporting `.txt` and `.nsf` before and
  after a change and diffing the results is the practical regression check,
  but note the text-import caveat under Known pre-existing bugs.
- **`EXPORT_TEST` builds:** `/verify` compares the tracker's register writes
  with the NSF driver running in `ExportTest.dll`. That DLL is not in this
  repo.
- **In a Linux session:** verify by reading call sites, searching
  case-insensitively (see Gotchas). Pure logic such as enums, tables and
  arithmetic can be checked by pasting the snippet into a scratch `.cpp` and
  compiling it with `g++`.

## Architecture (0.4.6)

Code lives in `Source/`. `resource.h`, `version.h`, `config.h` and
`FamiTracker.rc` sit at the root.

- **App shell:** `CFamiTrackerApp` (`FamiTracker.cpp`, global `theApp`) owns
  the singletons `CSettings`, `CAccelerator`, `CMIDI`, `CSoundGen`,
  `CChannelMap` and `CCustomExporters`. It is an MFC SDI app built from
  `CFamiTrackerDoc`, `CMainFrame` and `CFamiTrackerView`.
- **Document** (`FamiTrackerDoc.*`, about 4.7k lines): all module data plus
  `.ftm` I/O.
  - Each track (song) is a `CPatternData` (`PatternData.h`): a frame list,
    patterns per channel, rows of `stChanNote`.
  - Instruments: `Instrument*.cpp`, base class `CInstrument`, one subclass per
    chip, except that MMC5 uses `CInstrument2A03`.
  - Sequences (macros, `CSequence`) are stored per chip in
    `m_pSequences2A03/VRC6/N163/S5B`; FDS sequences live inside
    `CInstrumentFDS`.
  - DPCM samples: `CDSample`.
  - Limits are in `FamiTrackerTypes.h`: 64 tracks, 128 frames, 128 patterns
    per channel, 256 rows, 64 instruments, 128 sequences per type, 64 DPCM
    samples, 4 effect columns. Some are tied to the file format or NSF driver.
- **View and editors:** `CFamiTrackerView` handles input and owns
  `CPatternEditor` (`PatternEditor.cpp`), which draws the pattern grid into
  off-screen DCs. `CMainFrame` hosts the frame editor (`FrameEditor.cpp`), the
  instrument list, the toolbars and the undo stack. Instruments are edited in
  `CInstrumentEditDlg` with `InstrumentEditor*` panels: one per chip, two for
  2A03 (plus DPCM), FDS (plus envelope) and N163 (plus wave). Sequence editing
  is in `SequenceEditor`, `GraphEditor`, `SizeEditor` and `SequenceSetting`.
- **Playback:** `CSoundGen` (`SoundGen.cpp`, a `CWinThread`) runs the player
  and drives one `CChannelHandler` subclass per channel (`Channels2A03.cpp`,
  `ChannelsVRC6.cpp`, and so on). Handlers run effects and instrument
  sequences, then write chip registers through
  `WriteRegister`/`WriteExternalRegister` into `CAPU` (`APU/`). VRC7 and 5B
  run emu2413 and emu2149 inside `APU/VRC7.cpp` and `APU/S5B.cpp`; the mixer
  (`APU/Mixer.cpp`) mixes everything with Blip_Buffer, and
  `CDSound`/`CDSoundChannel` (`DirectSound.cpp`) play the result.
- **NSF export:**
  - `CCompiler` (`Compiler.cpp`) builds a list of `CChunk` objects;
    `CPatternCompiler` turns patterns into driver bytecode (`command_t`).
  - `ChunkRenderBinary` writes binary output and `ChunkRenderText` writes ca65
    assembly.
  - The 6502 driver ships only as prebuilt byte arrays with relocation tables
    in `Source/drivers/drv_*.h`, packaged in `Driver.h` and chosen per chip in
    `CCompiler::CompileData`. No driver assembly source exists in any commit,
    so new driver commands cannot be added from this repo.
  - `drivers/config.h` hard-codes each binary's vibrato-table offset and has
    to change whenever a binary does (the `arp-scheme` fork shows this).
- **Other:**
  - `TextExporter.cpp`: text import and export.
  - `CustomExporter*.cpp`: plugin DLLs loaded from `Plugins\` next to the exe,
    which export `GetName`, `GetExt` and `Export`. The API is in
    `CustomExporterInterfaces.h`.
  - `MIDI.cpp`, `PCMImport.cpp` with `resampler/`, `Visualizer*.cpp` with
    `FFT/`, `CommandLineExport.cpp`, and `Settings.cpp` (registry).

## Threading rules

- The two main threads are the MFC UI thread and `CSoundGen`, which runs at
  `THREAD_PRIORITY_TIME_CRITICAL`. Two more run code: the visualizer's worker
  thread (`CVisualizerWnd`, which draws with `GetDC()` off the UI thread) and
  the MIDI input callback (`CMIDI::MidiInProc`, on a system thread).
- UI to sound: `PostThreadMessage(WM_USER_*)` through the wrappers in
  `SoundGen.h` (`StartPlayer`, `StopPlayer`, `WriteAPU`, `PreviewSample`, ...).
- Sound to UI: `PostMessage` to the view (`WM_USER_PLAYER`,
  `WM_USER_NOTE_EVENT`).
- Some calls cross threads directly, without a message or lock: the UI thread
  calls `CSoundGen::LoadMachineSettings`, and the sound thread calls
  `CMainFrame::GetSelectedInstrument()` (through `GetDefaultInstrument`),
  `CVisualizerWnd::FlushSamples` and `ReportAudioProblem`, which calls
  `Invalidate()`.
- The sound loop is `CSoundGen::OnIdle`:
  1. `LockDocument(0)`, non-blocking. If the UI holds the lock, the player and
     channel updates are skipped for that tick; `UpdateAPU` still runs.
  2. `RunFrame`, `PlayChannelNotes`, `UpdatePlayer`, `UpdateChannels`.
  3. Unlock, then `UpdateAPU`. Waiting on the DirectSound buffer is what paces
     the loop.
- UI code that changes structure (channels, expansion chip, tracks) must hold
  `LockDocument()`. Instrument slots have their own critical section.
- The sound thread calls some `CFamiTrackerView` methods directly:
  `PlayerTick`, `PlayerGetNote`, `GetSelectedFrame/Row/Channel`,
  `GetAutoArpeggio` and `MakeSilent`. Keep those free of window and GDI calls.
- Live notes (keyboard or MIDI) go through `CSoundGen::QueueNote` into
  `CTrackerChannel::SetNote`, which is lock-protected, and are consumed in
  `PlayChannelNotes`.

## Data and file-format invariants

- **`.ftm` container** (`DocumentFile.cpp`):
  - Layout: `"FamiTracker Module"`, a `uint32` version (`FILE_VER` 0x0440),
    then blocks of `[16-byte NUL-padded ID][int32 version][int32 size][data]`,
    then `"END"`.
  - Fields are written one at a time as little-endian `int32` values or single
    bytes. Strings are fixed 32-byte fields (song info), NUL-terminated (track
    names) or length-prefixed (instrument and sample names).
  - Files newer than `FILE_VER` are refused. Unknown blocks are skipped. Block
    IDs are at most 15 characters.
- **Block versions:** each block has its own version. To add data:
  1. Append it in `WriteBlock_*` and bump that block's version.
  2. In `ReadBlock_*`, read it only when `GetBlockVersion()` is new enough.

  Old files must keep loading. `ReorderSequences`, `ConvertSequences` and the
  FDS octave transpose show the existing compatibility code.
- **`ASSERT_FILE_DATA`** asserts in Debug; in Release it makes the enclosing
  function return `true`. In `ReadBlock_*` that means error, but the macro is
  also used inside the instruments' `Load` functions, where `true` means
  success, so a failed check there reports success and loading continues out
  of step.
- **Serialized enums:** append only, never reorder `effect_t`, `inst_type_t`,
  `sequence_t`, `SNDCHIP_*` or `chan_id_t`. Patterns are stored by channel
  index. On load, the index-to-channel mapping is rebuilt from the chip in
  `chan_id_t` order (`CSoundGen::RegisterChannels`), so reordering `chan_id_t`
  scrambles existing modules.
- **Channel index vs. channel ID:**
  - Document and pattern APIs take the channel index: the column in the
    current module, `0..GetAvailableChannels()-1`. `GetChannelCount()` counts
    registered channels, which always include all 8 N163 channels even when
    the module uses fewer.
  - Sound code uses `chan_id_t` (`APU/Types.h`). Convert with
    `GetChannelType(index)`.
  - `CHANNELS` (29, the `chan_id_t` count) differs from `MAX_CHANNELS` (28,
    `FamiTrackerTypes.h`).
- **Empty cell:** `Note = NONE (0)`, `Instrument = MAX_INSTRUMENTS (0x40)`,
  `Vol = MAX_VOLUME (0x10)`, `EffNumber = EF_NONE (0)`. Notes 1-12 are C to B,
  13 is release and 14 is halt.
- **Effect letters:** `EFF_CHAR[]` is indexed by `effect - 1`. FDS and 5B
  reuse the letters H, I and J.
- **Instrument files (`.fti`):** `"FTI"` plus `INST_VERSION` "2.4".
  `CFamiTrackerDoc::SaveInstrument`/`LoadInstrument` write and read the header,
  version, type byte and name; each `CInstrument*::SaveFile/LoadFile` handles
  the data after that. Inside modules, instruments use `Store/Load`.
- **Linear pitch:** `m_bLinearPitch` is not saved (TODO in
  `WriteBlock_Parameters`).

## Recipes

**Add a pattern effect**
1. In `FamiTrackerTypes.h`, append to `effect_t` before `EF_COUNT` and add its
   letter to `EFF_CHAR` at index `effect - 1`.
2. If the letter is chip-specific, add it to that chip's table in
   `CFamiTrackerView::EditEffNumberColumn`.
3. Playback: shared effects go in `CChannelHandler::CheckCommonEffects`,
   chip-specific ones in the channel class's `HandleCustomEffects`.
4. NSF export is `PatternCompiler.cpp`. The driver is binary-only, so an effect
   it does not understand can only be dropped or approximated at export.
5. Check `TextExporter.cpp` and `hlp/effect_list.htm`.

**Add an undoable edit**
1. Add a value to `CPatternAction` or `CFrameAction`.
2. Save the old state in `SaveState()`, which then calls `Redo()` to apply the
   change.
3. Apply in `Redo()` and restore in `Undo()`.
4. Submit with `AddAction()`.

Only pattern and frame edits are undoable in 0.4.6; instrument, sequence and
module edits change the document directly.

**Add a program setting:** add a member in `Settings.h`, register it in
`CSettings::SetupSettings()` with `SETTING_INT`, `SETTING_BOOL` or
`SETTING_STRING` (saved to the registry automatically), and add UI in the
matching `Config*.cpp` page.

**Add a dialog or control:** edit `FamiTracker.rc` and `resource.h` by hand
and keep IDs unique. Help topic IDs are in `hlp/HTMLDefines.h`.

**Add a sound chip:** the `sneven` diff against `5fb684d` swaps the 2A03 for
an SN76489 and is the closest example to study, though only a test build: its
NSF export still uses the 2A03 driver. Touch points:
- `APU/Types.h`.
- An `APU/` emulator plus dispatch in `CAPU` and `CMixer`.
- A `Channels*.cpp` handler.
- `CSoundGen::CreateChannels` and `CChannelMap::SetupSoundChips`.
- Instrument and instrument-editor classes.
- Document sequence blocks.
- The compiler and driver.

## Gotchas

- **Case-insensitive filesystem assumed.** 20 distinct `#include` paths, on 31
  lines, differ in case from the files they name (`"apu/apu.h"`, `"FDS.h"` for
  `APU/FDS.H`, `"fft\fft.h"`, ...), and several files have upper-case
  extensions (`APU/APU.CPP`, `APU/VRC6.H`). Search with `-i`, and don't fix
  case outside a deliberate portability change.
- **CRLF line endings** everywhere except `gpl.txt`, `hlp/gpl.txt`,
  `Source/Blip_Buffer/Blip_Buffer.h`, `Source/resampler/resample.hpp`,
  `resample.inl`, `sinc.cpp`, `sinc.hpp` and `resample.cpp` (LF apart from one
  line). There is no `.gitattributes`, so keep CRLF when editing. Shell scripts
  (`.github/scripts/*.sh`) are the deliberate exception: they must stay LF,
  since bash chokes on `\r`.
- **Resource encoding:** `FamiTracker.rc` is Windows-1252
  (`#pragma code_page(1252)`). Never convert it to UTF-8.
- **`DEBUG_NEW`:** `stdafx.h` ends with `#define new DEBUG_NEW`, which applies
  to every file that includes it. Placement new or a custom `operator new`
  must account for it.
- **Precompiled headers:** PCH is off. 37 `.cpp` files include other headers
  before `stdafx.h` (`Compiler.cpp` and `FamiTrackerView.cpp` start with
  standard headers), and 29 never include it: all of `APU/` and
  `Transcribe/`, plus `Blip_Buffer.cpp`, `FFT/Fft.cpp`, `WaveFile.cpp`,
  `resampler/`, `ExportTest/` and the `CustomExporter*` files. Turning PCH on
  breaks them.
- **Instrument references:** `CFamiTrackerDoc::GetInstrument()` returns a
  retained pointer. Call `Release()` or use `CInstrumentContainer<T>`.
- **One expansion chip at a time:** `SNDCHIP_*` are bit flags, but the
  Module Properties dialog and `CCompiler::CompileData` accept a single chip.
- **Sunsoft 5B is half-built:** it is offered only in Debug builds, has no
  channels (`#if 0` in `CreateChannels`) and no NSF driver, and saving such a
  module is refused.
- **File associations:** Release builds register the `.ftm` file type on every
  start (per-user on Vista and later); Debug builds don't.
- **Dead code:** `ConfigWindow.cpp` is not in the project, though `MainFrm.cpp`
  still includes `ConfigWindow.h`.
- **Licensing:** Blip_Buffer is LGPL. `ReadMe.txt` says the FFT code (Reliable
  Software) is not GPL and the YM2413/YM2149 emulators may not be either. Keep
  that in mind before redistributing.

## Known pre-existing bugs (0.4.6)

Point these out rather than silently fixing them inside archive commits.

- **Effect-letter overread:** lookups in
  `CFamiTrackerView::EditEffNumberColumn` and in text import loop up to
  `EF_COUNT`, but `EFF_CHAR` has `EF_COUNT - 1` entries. When no letter
  matches, they read one element past the end.
- **Text import loses chip effects:** it takes the first matching letter in
  `EFF_CHAR`, so FDS and 5B `Hxx` and `Ixx` come back as the 2A03 sweep effects,
  and 5B `Jxx` becomes the FDS modulation-speed effect (FDS `Jxx` survives).
  Such modules don't round-trip through `.txt`.
- **No bounds checks on load:** `CDocumentFile::GetBlockInt`, `GetBlockChar`,
  `GetBlock` and `ReadString` don't check the block size, so a corrupt `.ftm`
  can read past the buffer.
- **Reference-count race:** `CRefCounter::Release()` re-reads the counter after
  `InterlockedDecrement` instead of using its return value, which allows a
  double delete.

## Code conventions

The code is MFC/Win32 C++ of the VS2008 era:
- `C` class prefix and Hungarian members (`m_iChannelID`, `m_pDocument`,
  `m_bPlaying`, `m_cName`).
- Tab indentation.
- `CString`, `TCHAR` and `_T()` for strings.
- `SAFE_RELEASE`/`SAFE_RELEASE_ARRAY`, `TRACE` and `ASSERT`.

New files carry the GPLv2 header used in `Source/*.cpp`.

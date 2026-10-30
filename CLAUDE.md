# CLAUDE.md

Guidance for working in this repository. This is the `beginner-friendly`
branch: FamiTracker 0.4.6 with a modernized build, plus the beginner features,
most of them built on the `recorder` branch and merged here in October 2026.
The audio-to-notes recorder is described in `RECORDER.md`: the engine in
`Source/Transcribe/`, the `wav2ftm` tool, Edit > Insert recording in
`Source/InsertRecording.cpp`, Edit > Record from microphone in
`Source/RecordDlg.cpp` (audio I/O in `Source/MicRecorder.cpp`), Edit > Add
chords and bass line in `Source/BackingDlg.cpp` (chords from
`Source/Transcribe/Harmony.cpp`, written by `InsertRecording.cpp`), clips in
`Source/Clips.cpp` (the song's clips and linked copies, saved in a `BF CLIPS`
block), shown in the clips panel docked along the right of the window
(`Source/ClipsPanel.cpp`; a clip dragged over the pattern editor is shaded by
`CPatternEditor::ShowClipDrop`) with the clip editor in `Source/ClipsDlg.cpp`,
and View > Piano roll in `Source/PianoRoll.cpp`: a control bar docked under
the pattern editor (told about edits through `CMainFrame::SongChanged`) whose note
edits are worked out by `Source/NoteEdit.cpp` and written as one undoable
action by `WriteChannelRows` in `InsertRecording.cpp`. Named sections (a
track's frames named Intro, A, B...) are `Source/Api/Sections.cpp`
(portable, shared with the tools): the document keeps them in a
`SectionStore` (`CFamiTrackerDoc::GetSections`, saved in a `BF SECTIONS`
block) and keeps them with their frames in its frame functions; the frame
editor draws them (`CFrameEditor::DrawSections`) and changes them with its
right-click commands as `CFrameAction`s (every frame action snapshots the
sections, so Undo puts them back), and the piano roll's ruler names them.
A track's key (`Source/Api/Keys.cpp`, portable too) is chosen in the piano
roll's Key menu or with `set_key` and kept in a `KeyStore`
(`CFamiTrackerDoc::GetKeys`, saved in a `BF KEYS` block), changed as one
undoable `CTrackKeyAction` (`SetTrackKey` in `InsertRecording.cpp`); without
one, the key is found from the notes. The tools use it as their default
(`TrackKey` and `KeyArg` in `Tools.cpp`), and the piano roll shades its notes,
snaps notes to them (Snap to key) and names the chords the parts play in a
strip under the ruler, from `ToolSet::Harmony` through
`CMcpConnection::Harmony` (worked out a moment after a change, then kept).
The overall plan is in `ROADMAP.md`. Unless a section says otherwise,
everything below describes the 0.4.6 code as found on `master` and was
checked against that source.

The Sunsoft 5B is a working chip here, and a module can have several
expansion chips on at once. The 5B's channels are `Source/ChannelsS5B.cpp`
(emu2149 in `Source/APU/S5B.cpp`), its instruments and sequences
`InstrumentS5B.cpp` and `InstrumentEditorS5B.cpp` (its duty sequence is the
chip's noise and mode), and its envelope effects `Hxy`, `Ixx` and `Jxx`.
Module > Module properties has a checkbox for each chip
(`ModulePropertiesDlg.cpp`), `CAPU` and `CMixer` run every chip that's on,
and `Source/ChipSwitch.cpp` works out the channels (`ChipChannels`) and,
when the chips change, keeps the notes of the chips that stay
(`CFamiTrackerDoc::SelectExpansionChip`, `CPatternData::MoveChannels`) after
asking before the channels of the others are cleared. A module with one chip
is saved byte for byte as 0.4.6 saves it; NSF export refuses several chips
and the 5B, which OGG and WAV export play. See Gotchas.

File > Connect Claude Code makes FamiTracker an MCP server for Claude Code
(see `MCP.md`; built on the `api` branch and merged into `recorder`, then
here). The protocol and the tools are in `Source/Api/` (portable, tested on
Linux), the Winsock listener in `Source/McpSocket.cpp`, and the glue to the
document, the settings and the dialog in `Source/McpConnection.cpp`, which
writes changes with `WriteSongParts` in `InsertRecording.cpp` (exactly as the
tools worked them out; only a clip placed keeps the song's own speed, jumps
and halts where it lands, as dragging one does; for `insert_bars` and
`remove_bars` it first puts whole frames into the frame list or takes them
out, so the frames after keep their patterns) and switches expansion chips
with `Source/ChipSwitch.cpp`. Claude Code in WSL connects
through `famitracker-mcp.exe` (`Source/Api/tools/famitracker-mcp.cpp`, logic
in `Source/Api/Bridge.cpp`), a stdio MCP server that WSL runs on Windows and
that passes each message to 127.0.0.1; it is built by the `Source/Api` CMake
project and ships next to FamiTracker.exe. Other computers on the user's
home network connect when the dialog's "Allow connections from other
computers on my network" is ticked (setting "Allow network"; see MCP.md,
Connecting from another computer): `CMcpSocketServer::Start` then binds
every IPv4 address and closes connections from addresses outside home
networks (`MayConnect` in `Source/Api/Network.cpp`: loopback, 10/8,
172.16/12, 192.168/16, 169.254/16, 100.64/10 for Tailscale, or this PC's
own addresses, which `ListNetworkAddresses` in `McpSocket.cpp` reads with
`GetAdaptersAddresses`), and each request carries its client's address
(`HttpRequest::Peer`, empty for this computer). `HandleMcp` checks the peer
again, accepts this PC's addresses and name in the Host header
(`NamesThisComputer`) and routes `/mcp`, `/mcp/core` (the 38 tools of
`ToolSet::CoreTools` with the short `ToolSet::CoreInstructions` and no
prompts, for local models), `GET /files/<name>` (the OGG files next to the
song, `SongHost::ReadExport`) and `PUT /inbox/<name>` (like the `send_file`
tool: `ToolSet::ReceiveFile` into the Inbox folder next to FamiTracker.exe,
`InboxFolder` in `Source/AppFolders.cpp`, `SongHost::SaveInbox`); a
request from the network gives `ToolSet::Call` a `Caller` whose `FilesUrl`
makes `export_for_game` link its files. `famitracker-mcp --host <address>
[--port N] [--token CODE] [--core]` reaches FamiTracker on another
computer; its sources build alone as the `ftbridge` library, which CI links
statically against musl on Alpine for the release's `famitracker-mcp-linux`.
File > Export for games saves
tracks as looping OGG files through the `export_for_game` tool
(`Source/Api/GameAudio.cpp`, `Ogg.cpp`): FamiTracker renders with `CSoundGen`,
which notes the sample where each row starts (`GetRenderMarks`), and libvorbis
encodes (`Source/libvorbis`, `Source/libogg`). The jingles that `make_jingle`
writes as tracks of their own are scores in `Source/Api/Jingles.cpp`. The
sound effects of `make_sound_effect`, tracks at speed 1 that halt (or loop,
for ambience), are built a tick at a time in `Source/Api/SoundEffects.cpp`,
from layers described in words (`DesignSoundEffect`, which the kinds from
nature use too), or made from a voice recording, and turned into
instruments, by `Source/Transcribe/SoundEffect.cpp`. Edit > Sound effect from your voice
(`Source/VoiceEffectDlg.cpp`) records a take, which `transcribe::VoiceTake`
ends when the sound is over, and adds it with that tool through `CMcpLink`.
Both dialogs keep their takes in the `Recordings` folder next to
FamiTracker.exe (`Source/AppFolders.cpp`, which moves takes from the old
Documents\FamiTracker recordings into it), where `make_sound_effect`'s
recording `"latest"` finds the newest (`SongHost::LatestRecording`).
Arranging is in `Source/Api/Chords.cpp` (chords read from names and degrees,
progressions on bars, colors, chords heard in the notes) and
`Source/Api/Arrange.cpp` (chord parts in styles, bass lines, drum grooves
with fills, a harmony voice, a counter-melody), used by
`add_chords_and_bass`, `make_groove`, `add_drums`, `add_harmony`,
`add_counter_melody` and `review_arrangement`; `design_instrument` turns words
into sequences with `DesignPreset` in `Presets.cpp`. Lush chords are
`LushShape` in `Arrange.cpp` (a chord's notes in arpeggio order) made into
instruments by `LushInstruments` in `Tools.cpp` (`WithArpeggio` in
`Presets.cpp`); FamiTracker reports each instrument's looping arpeggio
(`InstrumentInfo::Arpeggio`), which `ChordAbove` and `SoundsChord` in
`Tools.cpp` count as chords. `set_swing` alternates speed effects every
half beat (or, `by` delays, puts `Gxx` on chosen channels' off-beat notes:
`SwingByDelays`); `set_tempo` with `from_bar` writes speed and tempo
effects inside the song, at once or a step a beat (`WriteTempoChange`,
which `change_mood` uses for some bars too; `AddSongEffect` keeps a speed
and a tempo command on one row apart, since both are `Fxx`), and
`set_loop` with `stop` a halt (`StopSongTool`). `mix` scales each part's
volume column by its role and
sets the DPCM level (`Zxx`, also on every sample, since the kit's samples
start from silence), counting each chip's volume as FamiTracker plays it
(`LevelDb` and `VolumeForDb` in `Tools.cpp`, measured: a VRC7 sound about
5 dB over a pulse at 15 and 3 dB a step, a 5B square 0.4 dB over and 3 dB a
step, the N163's by its channel count (`N163FullDb`: a square wave 0.6 dB
under with 4 channels, 4.4 over with 1, the wave sounds' waves 4.1 dB under
a square on average; the tools count each chip's channels when they read the
song, `ChannelInfo::ChipChannels`), the FDS's about 5 dB over with its envelope
at the top), as
the echo, harmony, counter-melody, `polish` and the review do; `mix` and
the review also play each note's instrument envelope (`HeldEnvelope`, from
the volume sequence FamiTracker reports in `InstrumentInfo::Volume`, times
the volume column over 15, on the 5B their steps added: `PlayedVolume`),
and `mix` moves volume slides
with the volumes (`MixSlides`, which follows a swell too slow to scale a
step at a time). `remove_unused_instruments` counts each instrument's
cells in every track and clip (`SongHost::CountInstrumentUses`) and
removes the unused ones as Edit > Clean up does (`RemoveInstruments`,
`CFamiTrackerDoc::RemoveUnusedSequences`; that menu command keeps clips'
instruments too), and
`Vrc7Customs` in `Presets.cpp` are VRC7 custom patches of our own design
(`AddVrc7CustomInstrument` in `InstrumentLibrary.cpp`). Sampled drums are
`DrumOptions::Samples` in `Arrange.cpp` (which hits a sample plays) written
by `DrumChanges` in `Tools.cpp` onto the noise and DPCM channels;
`add_chords_and_bass` finds the chord part, bass and chord voices already in
its bars (`ChordPartIn`, `BassPartIn`, `ChordVoicesIn`) to write over them,
and strums voiced chords with `Gxx`; `polish`'s touches are `Accents`,
`MakeVibrato`, `AddFades`, `AddScoops`, `AddFalls`, `StaccatoBass` and
`SeparateRepeats`. The tools use all four effect columns: `Session` hides
the effects of columns a channel doesn't show (they don't play), and
`CInsertRecordingAction` shows more columns where written effects need them
(undoably).
`Source/Api/PulseClicks.cpp` plays the 2A03's and MMC5's pulse notes a
frame at a time as the 0.4.6 channel handlers do (sequences, `4xy`, `Pxx`,
slides, volume): a vibrato that takes a held note's period across a multiple
of 256 clicks (see Gotchas), and `Commit` nudges the notes a direct change
leaves clicking with `Pxx` (`SmoothChanges`), `check_song` lists them,
`polish`'s `clicks` touch fixes older ones and `export_for_game` warns.
`check_song` and `review_arrangement` also find notes that vanish
(`VanishingNotes` in `Tools.cpp`): a run of a few notes on a sound gone
within four frames between held notes of their line, which a user heard as
missing notes, and `design_instrument` and `reference_sound` say when the
sound they add is that short (`VanishRemark`).
View > Problems (`Source/ProblemsPanel.cpp`, docked along the bottom or the
right) shows those checks in FamiTracker: it runs `check_song` with
`format` `json` (each finding as data: `CheckFinding` and `CheckJson` in
`Tools.cpp`) through `CMcpConnection::RunTool`, which runs a tool for
FamiTracker's own windows with direct changes whatever the Claude Code
setting says. `Source/Api/ProblemList.cpp` reads the JSON back (a line a
note for notes that vanish) and keeps the marks that
`CPatternEditor::DrawRow` (a tick at the channel's edge) and the piano roll
(a dot on the note) draw through `ProblemMarkAt`. The panel checks again a
moment after `CMainFrame::SongChanged`, not while the song plays, and
polls a signature of what changes unannounced (instruments' sequences,
speed and tempo, the track); a click's Nudge runs the `fix` the JSON gives
(`polish` with the touch `clicks` on its bar, one undoable action), and
Show selects notes in the piano roll (`CPianoRollWnd::ShowNotes`).
`Source/Api/Effects.cpp`
holds the `effects` tool's reference to 0.4.6's effects and techniques, and
the checks `write_notes` makes on effects (`ParseChannelEffect`), taken
from the 0.4.6 channel handlers. `Source/Api/Wavetables.cpp` draws the N163's
wave sounds (16-sample waves, in tune in 0.4.6, played in turn by their wave
sequence) and places each one's wave in the chip's shared memory
(`FindWavePlace`); `AddWaveSoundInstrument` in `InstrumentLibrary.cpp` makes
the instrument. `Source/Api/FdsSounds.cpp` draws the FDS's sounds (a wave of
64 samples of one cycle, the modulator's table, speed, depth and delay, and
the volume, arpeggio and pitch sequences CInstrumentFDS keeps itself);
`AddFdsSoundInstrument` in `InstrumentLibrary.cpp` makes the instrument, for
the tools (`SongHost::AddFdsInstrument`) and for Instrument > Sound
browser; `DesignFdsSound` makes `design_instrument`'s FDS
sounds (a wave by word, a `SoundDesign`'s sequences turned into the FDS's,
the modulator by word). FamiTracker reports an FDS instrument's volume
(0-31) as the others' 0-15 (`FdsVolumeAs15`), so the mix, the review and
the vanish check hear it, and the tools write FDS notes two octaves up
(`Written` in `Tools.cpp`; see Gotchas). A study keeps an FDS sound's wave
and modulator (`nsf::StudySound`, then `ReferenceSound`'s `FdsWave` in the
reference's JSON), which `ReferenceFdsSound` makes into an FDS instrument
again (and another chip's sound into one, with a wave from its channel),
and `ReferencePreset` counts its volume 0-15 for a pulse. The server's
instructions (`ToolSet::Instructions`) teach the arranging craft, and the
recipes of `ToolSet::Prompts` are served as MCP prompts. `soundtrack_notes`
reads and writes a text file next to the song through `SongHost::ReadFile`
and `SaveFile`.

File > Import NSF (`Source/ImportNsfDlg.cpp`) opens an NSF's tracks as a
new module through the NSF importer in `Source/Nsf/` (portable: a 6502,
the chips' registers, the player that records and finds loops, the
conversion to a module in FamiTracker's text format, which
`CTextExport::ImportText` reads, and `Study.cpp`, which describes a track
for a composer). Claude Code's `study_nsf` keeps those studies in a
reference library, JSON files in the `References` folder next to
FamiTracker.exe (`Source/Api/References.cpp`, through `SongHost::ListLibrary`
and its siblings; `ReferencesFolder` in `Source/AppFolders.cpp` moves
references from the old Documents\FamiTracker references into it, and falls
back to Documents where FamiTracker's folder can't be written), with
`references`, `reference_sound`
and `import_nsf`, and a built-in style guide in `Source/Api/StyleGuide.cpp`. `study_module` does
the same for tracker modules: `Source/Nsf/Tracker.cpp` reads the patterns
of MOD, S3M, XM and IT files (never their samples), and
`Source/Nsf/MidiFile.cpp` the notes of MIDI files (each part on channels of a
note at a time, the drums a channel per kind, the meter kept so the study
counts its bars), into a module of one track whose channels are its own
(`Module::ChannelNames` and `ChannelKinds`), which only a study reads.
FamiTracker's own modules are read by `Source/Nsf/FtmFile.cpp` as 0.4.6's
`OpenDocumentNew` reads them (every block version, with its conversions for
old files, every read checked against its block), into a `Module` that
`WriteText` writes as File > Export text does; `study_module` studies each
track (`FtmTrackReport` gives its tempo and beat) and keeps its sounds.
`nsf2ftm` (`Source/Nsf/tools`) runs the importer from a command line, studies
modules with `-study`, and ships next to FamiTracker.exe; `ftm2txt` writes a
module's text.

Importer fixes (from `feat-importer-fixes`): while a track plays,
`RecordTrack` takes a repeat for its loop only once it has gone on for
`RecordOptions::ConfirmSeconds` (40) and two more times through (`Confirm`
in `FindLoop` and `FindLoopApprox`, `RepeatNeeded`), so neither a passage
played twice inside a piece that then moves on nor a vamp inside a long
loop is taken for the loop; a recording that runs to its end takes a loop
heard once more. A loop found a tick either way takes the length a tick
longer when its notes fall on the very same ticks clearly more often, and
an exact repeat a tick or two longer when there is one (`ExactStart`).
When only the melodic channels repeat, theirs is the loop and the drums'
own cycle is `Recording::DrumLoop` (`DrumsApart`). `AudibleLoopStart`
moves a loop's start back over a first time through that differs too
quietly to hear (a quiet pulse's tails or duty, a note a tick early, a
sweep's own clock), when that reaches the track's start or a second or
more. A track that falls silent for good well before its playlist's
length is surveyed (`SilentBeforePlaylist`) and studied as what it plays
(`Recording::LastSound`; `TrackStudy::Kind` "sting", a jingle under 12
seconds, or "sound", `Seconds`, `PlaylistSeconds`). So `SurveyTracks`
plays every track with a playlist length too (up to its 45 seconds),
which File > Import NSF does on the UI thread behind a wait cursor: about
2 seconds for Castlevania III's 28 tracks on the slow Linux test machine.
A DPCM part whose samples sound four or more pitches
as a line (`PlaysALine`; a sample's pitch at a rate from `SamplePeriod`,
its wave's period by autocorrelation, and `SampleNote` in `Convert.cpp`)
gets "DPCM bass" instruments whose keys play each sample at the note it
sounds (all written octaves up, and named so, when the line goes under
C-0; the unpitched samples stay on the "DPCM samples" kit), and the study
describes it as a part (`C_DPCM_LINE`, `FindDpcmLine`), the bass when it's
the lowest, so its notes count in the chords. `KeyOf` in `Study.cpp`
weighs the profiles' likeliest keys by the bars' chords, the bass and
where the music comes to rest (the loop's first chord, a piece's first and
last, a loop's end on the dominant): "C (major and minor)" when the tonic
chord comes both ways, "no clear key (around E)" or nothing when the
chords keep leaving the key in either mode or a short piece that plays
once stops on a chord of neither. `FindGrid` takes rows of 4 and 5 ticks
in turn when they fit. `SimulateTrack` keeps the samples it plays, so
`Compare` knows the DPCM's by their bytes. When `study_nsf` studies a
track again, `KeepOwnTags` in `References.cpp` keeps the user's tags and
drops the old study's own. `TestDpcmBass`, `TestShortOfPlaylist`,
`TestQuietFirstPass`, `TestPassageRepeatedOnce`, `TestLoopAroundARepeat`,
`TestDrumsOnTheirOwn` and `TestKeys` in `Source/Nsf/tests/NsfTests.cpp`
and `TestStudyTagsAgain` in `Source/Api/tests/ApiTests.cpp` cover them.

The steps of `ROADMAP.md` that make FamiTracker itself simpler: Instrument >
Sound browser in `Source/SoundBrowserDlg.cpp` (the instrument library's
menu command `ID_INSTRUMENT_LIBRARY` and Ctrl+Shift+L): one list of every
sound the song can get, the index `Source/Api/SoundIndex.cpp` (portable,
tested in `TestSoundIndex`): `ListSounds` lists the ready-made sounds for
each kind of channel the song has (`sound_target`; the presets of
`Source/Api/Presets.cpp`, the 5B's, the VRC7's patches and custom patches,
the N163's and the FDS's sounds), every sound of the reference library's
studies for each channel it can play on (at home where `reference_sound`
puts it without a channel) and the song's own instruments (on the channel
the track plays them most, `SongSoundUses` in `Tools.cpp`), each with its
kind (`KindOfWords`), words, origin and the warnings of notes that vanish
(`SoundHeardFrames`, `VanishWarning`: the check and text of `VanishRemark`)
and of a vibrato that clicks on the 2A03's pulses (`VibratoClickPitches` in
`PulseClicks.cpp`); `FindSounds` searches its precomputed words
(`IndexWords`) and filters it, and `PreviewSteps` gives the notes a preview
plays. The browser gets it through `CMcpConnection::Sounds` and makes a
sound through `CMcpConnection::MakeSound` (`ToolSet::Sounds` and
`MakeSound`, as `add_instrument` and `reference_sound` make them; the
latter's making is `MakeReferenceInstrument`), with the connection's own
dialog flag set so the host allows the change while the dialog is open;
`find_sound` searches the same index. A sound not in the song is tried as
an instrument and removed again with `RemoveInstrumentAndSequences` in
`Source/InstrumentLibrary.cpp` (where the presets become instruments, for
the tools too); `CFamiTrackerDoc::RemoveInstrument` makes the channels
forget an FDS instrument's sequences first. The note keys play the sound
picked through `CFamiTrackerView::NoteOfKey`, and MIDI notes come to the
browser instead of the pattern editor while it's open
(`CFamiTrackerView::SetNoteListener`). View > Drum grid in
`Source/DrumGridPanel.cpp` (the arithmetic in `Source/DrumGrid.cpp`), View >
Profile in `Source/Profile.cpp` (kept in `CSettings::Profile`; its template
fills new songs in `CFamiTrackerDoc::OnNewDocument`, and
`CMainFrame::ApplyProfile` rebuilds the main menu and lays the song settings
out for BPM), and Help > Tutorials in `Source/Tutorial.cpp`, which plays the
tutorials of `res/tutorials` (built in as RCDATA, read by
`Source/TutorialScript.cpp`) and hears about commands through
`CMainFrame::OnCommand`. New work goes to the `next` branch first; once its
build has been tried, `beginner-friendly` is fast-forwarded to it.

Views you can switch on and off (from `feat-views`): View > Pattern editor
(`ID_VIEW_PATTERNEDITOR`), View > Piano roll and View > Drum grid (the same
command as Edit > Drum grid, `ID_EDIT_DRUMGRID`) are check marks in
`CMainFrame`. One of the pattern editor and the piano roll always shows
(`Source/NoteViews.cpp`, portable, tested in `Source/Tests`):
`CMainFrame::ShowNoteViews` shows the piano roll before it hides the view
window, and `CPianoRollBar::Fill` gives the piano roll all the room the
other bars leave. Hidden, `CFamiTrackerView` stays the active view (commands
still route to it), keeps its pattern editor's size (`CalcWindowRect`),
passes the focus on to the piano roll (`OnSetFocus`, so every
`GetActiveView()->SetFocus()` lands there) and takes the piano roll's note
keys through `PianoRollKey` (the piano roll's letters D, B, S and E are notes
then); Edit > Cut, Copy, Paste, Delete and Select all go to the piano roll
when it has the keys (`EditCommand`). The accelerators work whichever window
has the keys. The program setting `Views.bPatternEditor` remembers the
pattern editor; the piano roll and the drum grid keep their own registry
sections, as the other panels do. `CMainFrame::ArrangeBars` lays the docked
panels out in one order (mixer, problems, drum grid, then the piano roll and
the clips panel, swapped while the piano roll fills), and each panel's
creation calls it. Those along the bottom the main window places itself, from
each one's `WM_SIZEPARENT` (`CMainFrame::PlaceBottomPanel`, since
`feat-newcomer-fixes`): the mixer along the edge, the problems panel, What
next and the drum grid side by side in one row above it (`ShareRow`, a
dragged line sets the whole row's height: `BottomPanelDragged`). The room
goes first to the note views as far as enough lanes to play in, then to the
row's and the mixer's least (`BottomPanelLeast`, at the panel's width: the
drum grid's buttons take more lines when narrow, `HeightFor`), then to the
note views as far as a third of the window (`NoteViewsKeep`), then to the
bands as tall as they were made (`FitPanelBands` in `NoteViews.cpp`; with all
four open the piano roll had one lane left on a window 1000 pixels high). The
panels are listed as `CMainFrame::RecalcLayout` starts, since each one's show
is made during the layout (`RecalcDelayShow`), which forgets it was to be
made. The drum grid is a panel, `CDrumGridBar`, docked along
the bottom or the right like the problems panel: it shows the bar the
cursor is in (`FollowCursor` in `DrumGrid.cpp`; its < and > keep a bar until
the cursor moves), flows its buttons into lines (`FlowDrumControls`), writes
with `WriteChannelRows` leaving the cursor where it is, and a song change in
the middle of a drag (Ctrl+Z, Claude) ends the drag unwritten. A tutorial
step shows the hidden part it's about (`ShowTutorialPart`: pattern, clips,
piano-roll, drum-grid).

Stereo is this branch's own (the NES, NSF files and FamiTracker 0.4.6 are
mono). Every channel has a pan, set by the `8xx` effect (`EF_PAN`, appended
to `effect_t`; `00` left, `80` the middle, `FF` right, lasting until the next
`8xx`, every channel starting in the middle at `ResetChannel`).
`CChannelHandler::HandleNoteData` handles it for every channel, the DPCM's
too, with `SetPan`, which tells `CAPU::SetChannelPan` and
`CMixer::SetChannelPan` (mapping the N163's channels, see Gotchas). `CMixer`
renders a left and a right Blip_Buffer with the pan law of `PanGains`: the
side a channel moves away from fades out and the other keeps all of it, so
a channel in the middle has a gain of exactly 1 on both sides. The 2A03's
two outputs mix each side's share of their channels with the same
nonlinear formulas; the VRC6's, MMC5's, FDS's and 5B's channels send each
side its share of their level (`MixSides`; `CS5B` sends each channel's level
as it changes); the N163's one DAC is followed channel by channel
(`N163Channel`); the VRC7 makes each side's samples with its channels
weighed by `PanGains256` (`OPLL_calc_lr` in emu2413; emu2149's
`PSG_calc_lr` is unused since the 5B's emulation sends its levels to the
mixer). `CSoundGen` plays and writes WAV files in stereo
(`OUTPUT_CHANNELS`), gives the visualizer the two sides together and counts
`GetRenderMarks` in samples of each side; `export_for_game` encodes stereo
OGG files (`EncodeOgg` with both sides; `transcribe::ReadWavSides` reads the
rendering) and measures loudness over both channels as BS.1770 sums them. A
song without `8xx` comes out on both sides sample for sample as the mono mix
did (`Source/Tests/ApuTests.cpp` checks every chip against hashes of the
mono code) and saves byte for byte as before; NSF export leaves `8xx` out
(`CPatternCompiler`), so its NSF is the same as without the pans. The piano
roll draws a note's pan as an arrow and pans notes from its Effects menu
(`CChannelNotes::SetPan`; every note edit keeps each note at the pan it
played at, and a new note plays where the channel is panned). The Claude
Code tools read pans into `NoteInfo::Pan`, write them with `write_notes`'
`pan`, `set_pan`, `add_echo`'s `opposite` and `mix`'s `width`, keep them
through their other writes (`BuildPart` keeps the old pans where a part
doesn't set its own, `KeepFollowing` the next note's), and `Commit` writes
the loop's first note its pan when a change leaves the song's end panned
otherwise (`KeepLoopPans`).

View > Mixer (`Source/MixerPanel.cpp`, a control bar docked along the
bottom like the problems panel, `AFX_IDW_CONTROLBAR_FIRST + 35`) gives every
channel of the module a strip: name and chip, a meter (the
`CTrackerChannel` volume meter the sound thread sets, read without a lock,
scaled by the fader), mute and solo (`CFamiTrackerView::ToggleChannel` and
`SoloChannel`, polled with the meters so the pattern editor's header and
the strips agree), a fader (-24 to +6 dB) and a starting pan. The settings
are `ftapi::MixStore` (`Source/Api/ChannelMix.cpp`, portable), kept by
`chan_id_t` in `CFamiTrackerDoc::GetChannelMix` (the same in every track;
`KeepOnly` drops channels a chip switch removes) and saved in a `BF MIXER`
block. After changing them the document calls `ChannelMixChanged`, which
copies them into plain arrays of `CSoundGen` (`SetChannelMix`, the UI
thread, no lock); the sound thread's `ApplyChannelMix` (every frame in
`OnIdle` and in `BeginPlayer`) gives `CMixer::SetChannelGain` the levels
and starts each channel at its pan in `CChannelHandler::ResetChannel`
(`GetStartPan`; `MixerPanChanged` moves a channel no `8xx` has moved yet).
`CMixer` plays a VRC6 or MMC5 channel off 0 dB through a Blip synth of its
own at the chip's volume times the level (`SynthVRC6Own`, `ClearOwn`: their
levels are 0-15, so scaling them would round a fader away), folds the
level into the side gains of the FDS, N163, 5B and VRC7 (`m_dSide`, which
`MixSides` and `PanGains256` use; their synths count too coarsely for a
gain of their own) and, on the 2A03, scales the channel's share of its pin
(the pin's formula is in proportion to its inputs), so the chip
compresses as before (its pins' whole steps leave a fader about 0.2 dB
off). At 0 dB nothing changes, sample for sample (`ApuTests`' hashes;
`TestLevels` checks halves, doubles and a silenced channel). Every mixer
change is one `CChannelMixAction` (`SetChannelMix` in `InsertRecording.cpp`;
a drag is one, with the settings before it). NSF export leaves the mixer
out and says so (`CCompiler::CompileData`). Write into the notes runs
`set_mixer` with `write_into_notes` through `CMcpConnection::RunTool`:
`WriteMixerIntoNotes` in `Tools.cpp` scales each channel's volume column
in every track as `mix` scales a part (`WriteMixerTrack`), writes the pans
as `8xx` on first notes and sets the mixer back, all inside
`SongHost::BeginGroup`/`EndGroup`, which `CMainFrame::BeginActionGroup`
makes one Ctrl+Z (`CActionGroup`). The tools see the faders and pans in
`ChannelInfo::MixLevel` and `MixPan`; `LevelDb` and `VolumeForDb` count the
fader, so every balance worked out from them does.

The mixer's depth (this branch's own too, off unless set): each strip has
two knobs, Soft and Space (0-100, `MixStore::SetDepth`; `PaintKnob` and
`ChangeDepth` in `MixerPanel.cpp`), and the top bar the space they send to
(room, echo in 48ths of a beat, repeats: `MixStore::SetSpace`, one per
song). The sound is `Source/APU/Space.cpp` (plain C++): `CSoftFilter`, a
one-pole low-pass from 12 kHz (1) to 1.8 kHz (100), and `CSpace`, an
eight-line FDN reverb (Hadamard mixing; each room's size, decay, damping,
pre-delay and diffusion, its wet level calibrated to 0.6 of a steady send
with a falling spectrum) with a stereo echo whose repeats feed it, all
allocated in `Allocate`, nothing on the sound loop. `CMixer::SetChannelDepth`
mixes a channel with either on into two Blip_Buffers of its own
(`m_OwnBuffer`, kept in step with the sides: cleared with their `offset_`
when it starts, `end_frame`d with them, read for a second after it stops),
moving what it had sent between them at that moment (`MoveOwn`): the
2A03's channels get their share of their pin in whole synth steps taken out
of the sides' step (`SplitPin`, so the two add up to the pin's steps
exactly), the N163's DAC level sits with the channel it plays
(`m_iN163Dest`), and the VRC7 makes samples of their own for such channels
(`OPLL_calc_lr_ch`, clipped apart). `ReadDepth` adds them back softened,
sends them (after the fader) and adds the space; `ReadBuffer` takes the old
path, sample for sample, while no channel has its own buffers and the space
is idle. `CSoundGen::ApplySpace` times the echo from the playing track's
speed, tempo and first highlight (not its `Fxx`) as each song starts; a
render goes on while `CAPU::SpaceRinging` (up to 15 s), so WAV files keep
the tail. `CMixer::ClearBuffer` (every `CAPU::Reset`, so every stop and
halt through `MakeSilent`) leaves the space ringing; `CAPU::ClearSpace`
silences it as a song starts (`BeginPlayer`) and as a render starts
(`OnStartRender`), so renders repeat exactly. `export_for_game` passes `CutForGame` `Tail` when a channel sends
to a space that sounds (`SpaceRings` in `Tools.cpp`): the loop is the second
time through, and a halt keeps its tail. `set_mixer` takes `soft`, `space`,
`room`, `echo` and `repeats` (`ParseRoom`, `ParseEcho`) through
`SongHost::SetMixer(MixerSettings)`; the tools' loudness counts a channel's
level before its depth.

`measure_mix` (`MeasureMixTool` in `Tools.cpp`) renders the selected track
through `SongHost::RenderTrack` as `export_for_game` does and measures it
(`SpansLoudness` in `GameAudio.cpp` takes stretches of a render as one
program). Each part alone (a stem) is a render with `RenderOptions::Silent`
(`Song.h`): `CSoundGen::RenderToFile` takes the channels to leave silent (by
`chan_id_t`) and whether to render the space's tail, and `ApplyChannelMix`
leaves them out of the mixer while the render runs (applied again as a
render starts and stops, `m_bMixAgain`), so their notes and effects still
play and their Space sends go with them. The other chips' channels get a
gain of 0; a 2A03 channel comes off its pin as the pin plays it
(`CMixer::SetChannelSilent`, `Pin1`, `Pin2`: the pin's output less what it
plays from the silent channels alone), since the pin compresses what it
mixes: a fader's share of the pin (a gain of 0, or the -24 dB stems
composers made by hand) leaves a triangle under a DPCM level several dB
louder than it sounds (`TestSilentParts`). The document's mixer, mutes and
track stay as they are. They're rendered in one pass (below), as far as the
mix needs, without the tail. A song that sets the
DPCM level (`Zxx`) on its first row starts with the DMC's output there
(`CSoundGen::StartDacLevel` in `BeginPlayer`, `CAPU::StartDAC`,
`CDPCM::StartAt`, and `CMixer::StartAt`, which takes the pin's output in
without a step), so playback and renders don't pop; `ExportTrack`'s fade-in
over the first 40 ms (`StartStep`) is older than that and stays.

VRC6 Plus (`Source/Api/Vrc6Plus.cpp`, portable) is this branch's own chip:
the VRC6 with a soft layer of four N163 wave channels, shown as one chip.
Underneath the module has the VRC6 and the N163 with 4 channels; the
document's mark (`CFamiTrackerDoc::IsVrc6Plus`, `SetVrc6Plus`, saved in a
`BF VRC6PLUS` block) holds only while those chips are on
(`ApplyExpansionChip` unmarks it otherwise), and `UpdateChannelNames`
renames the N163's first four tracker channels Soft 1 to Soft 4 while it
holds (`CTrackerChannel::SetChannelName`; the channels are the sound
generator's). Module > Module properties has its box (`IDC_EXPANSION_VRC6PLUS`:
the VRC6 and N163 boxes ticked and disabled while it's on), and choosing it
adds what's missing of its starter sounds (`AddSoftLayerSounds` in
`InstrumentLibrary.cpp`, from `ftapi::MissingSoftSounds`: choir pad, string
pad, soft flute and sub bass of `Wavetables.cpp`). Its channels are named as
the VRC6 Plus's by `IsVrc6PlusChannel` in the pattern editor's header
(`HeaderName`, which counts it as one chip), the mixer's strips,
`RecordingChannelName` (the piano roll's and other pickers) and the tools
(`ChannelInfo::Group` "VRC6 Plus" with `Chip` still "VRC6" or "N163",
`OldName` "Namco 1", which `ParseChannel` still takes; `SongInfo::Vrc6Plus`
with `Expansion` still "VRC6, N163"). `SongHost::SetExpansion` takes
"VRC6 Plus" in place of the two (`ChipFlags` in `McpConnection.cpp`), and
`set_expansion` adds the starter sounds itself. The sound index lists the
soft layer as `TARGET_SOFT`; `mix` keeps its parts `SOFT_LAYER_DB` under
their roles; voiced chords default to the string pad there.

Calm sounds (from `feat-calm-sounds`): `DesignOfKind` in `Presets.cpp` has
three calm leads, music box, breath and falling strike, which are also
presets of those names (the kinds at 75 BPM, `CALM_BEAT_FRAMES`);
`design_instrument` fits their times to the song's tempo (`FitCalmTempo`)
and their levels to a volume given (`FitCalmPeak`). `VIBRATO_FAINT`
(`FaintVibratoItems`) puts the period a step flatter for half of each
cycle, so it never takes a 2A03 pulse note across a multiple of 256.
`DecayItems` stretches the quadratic decay so a decay of N frames reaches
its level on frame N; in `Envelope` a held note's restrike is a two-frame
dip and a strike over the hold on the beat's grid from the note's start,
and a sound that dies away is struck again from its peak until a release.
`SoundDesign::Vrc6Duty` is the VRC6's own duty (6.25% steps, `DutyArg` in
`Tools.cpp`). The reply's volume a frame at a time is `VolumeTimeline`,
read from the instrument as the host reports it. The soft layer's calm
shapes are `SoftLayerCalmSounds` in `Vrc6Plus.cpp` (breath, quick pad,
pulsing pad, music box: not starters, since a song holds 8 N163 sounds),
and on an N163 channel `design_instrument` makes a `DesignedWaveSound`
from a built-in sound's waves. `WaveSoundDb` in `Tools.cpp` (from
`WaveLevelDb` and `HeldVolume` in `Wavetables.cpp`) is a wave sound's
level against a 2A03 pulse at 15: it matched the soft layer's eight
sounds measured under Wine within 0.5 dB. `TestCalmKinds`,
`TestDecayFrames`, `TestRestrikes`, `TestFaintVibrato`,
`TestSoftCalmSounds`, `TestVrc6DutiesAndStrikes`, `TestSoftDesign` and
`TestSoftLevels` in `Source/Api/tests/ApiTests.cpp` cover them.

Arranging fixes (from `feat-arrange-fixes`): chord parts keep the room
the melody and the bass leave them (`ChordPartOptions::Above`, `Below`,
`Ceiling` and `Floor`: `CHORD_ROOM` under the lowest melody note sounding,
`BASS_ROOM` over the bass), worked out by `Room`, `RoomFor` and
`RoomyPitch` in `Arrange.cpp` for every style (`ArpeggioVoicing`'s floor
and ceiling, `BrokenTones`, `GuidePitch`, the lush chords' roots and
`VoicedChordParts`, which `AddBackingTool` gives the melody's and the
bass's notes as they sound); `WalkLine` picks approach notes that don't
grind a semitone against the chord, and `ColorChords`' rich colors keep
each chord's function. Lush chords from one of the song's own instruments
take its sequences as a preset's (`SongInstrumentNamed` and
`SoundOfInstrument` in `Tools.cpp`; FamiTracker reports a 2A03 or VRC6
instrument's duty sequence in `InstrumentInfo::Duty`, read by
`ReadSequences` in `McpConnection.cpp`). The counter-melody's rests are
`CounterOptions::Rests` (`counter_rests`: `MelodyRests` and `Breathe` in
`Arrange.cpp`), and a note it would hold past a bar steps on (`MoveOn`).
`HarmonyPart` follows where each melody note lands (a slide on its first
row counted, `SlideSemitones`) and slides to the voice's own notes. An
echo note's volume is `EchoVolume` (each sound as `PlayedDb` plays it:
the chip's level and the instrument's envelope, the fader left out); the
echo's and the harmony's effects pass `CarriesEffect`
(`ChannelPlaysEffect` in `Effects.cpp`: an FDS lead's `Hxx`-`Jxx` stay off an
echo on the N163 or a pulse, `TestEchoKeepsItsChipsEffects`), and `EchoWithoutRubs` counts only
semitones and minor ninths, as the parts sound. `write_notes` keeps a
channel's detune where it was (`PartOptions::KeepDetune` in `BuildPart`),
`check_song`'s key check follows a note's slides (`SoundingRows`), and
`Picture.cpp`'s font has the punctuation of section names.
`TestArrangeFixes` in `Source/Api/tests/ApiTests.cpp` covers them.

Calm and lively tracks (from `feat-calm-defaults`): `ChooseStyle` in
`Tools.cpp` (near the top, with `NameSaysCalm`, `LooksCalm` and `IsDrumHit`)
gives `mix`, `polish`, `check_song` and `review_arrangement` their style:
calm only when asked or when the track's name says so (`VersionMood`), and
a line suggesting it when a song only looks calm (slow, no drum kit). The
estimate of a part's loudness as heard is the `Hearing` class before
`MixTool`: each channel's power frame by frame as the 0.4.6 handlers play
it (`SequenceRun`; `TimbreDb`, the K-weighted timbre of each pulse width,
the sawtooth, the triangle and the N163's waves, which
`InstrumentInfo::Duty` and `Waves` report; `PulseDacDb`, `TrianglePinShare`
for the triangle under the DPCM level and its fader, `PanDb`,
`SoftCutoff`), and `Loudness` gates 400 ms blocks as BS.1770 does, with
the `HEARD_*` constants measured against FamiTracker's renders.
`HearBalance` and `BalanceText` give each role against the melody where
they play together, `PlacesFor` where calm and lively put them, and
`MixTool`'s heard path sets the volumes (and the triangle's DPCM level)
from them; a part left half a dB or more off its place tries the volumes
around its own (the envelope's rounding counted) and keeps the nearest.
`ShapeOf` and `HeldLeads` find a lead whose sound holds a high, steady level (`check_song`'s `FOUND_HELD`,
`PROBLEM_HELD` in `ProblemList.h`; calm tracks only, see Lively stages below). `TestCalmChoice`, `TestHeardBalance`,
`TestCalmMix`, `TestCalmPolish`, `TestHeldLead`, `TestMixFixes` and
`TestReviewFixes` cover them.

Recipes (from `feat-recipes`): `compose_track` takes `kind` (calm, lively
or action) for `KindRecipe` in `Tools.cpp`, twelve steps written for that
kind that end with the style guide's topic of its name (`StyleGuideTopic`
in `StyleGuide.cpp`: `calm`, `lively`, `action`), the numbers a composer
would otherwise look up; without a kind the general recipe points at them.
`get_recipe` (`RecipeTool`) serves the same on every connection; asked
through `/mcp/core` (`Caller::Core`, set in `Mcp.cpp`) it gives
`CoreRecipe`'s steps, which name only the small set's tools. The tests
check that every tool a recipe names exists, and on the small set is in it.

Example songs (from `feat-examples`): Help > Example songs (a submenu of
`ID_HELP_EXAMPLE_*`, `CMainFrame::OnHelpExample`) opens one of the songs in
`res/examples` with its walkthrough, both in FamiTracker.exe as RCDATA
(`IDR_EXAMPLE_*`, the table `EXAMPLES` in `Tutorial.cpp`). `OpenExampleSong`
writes the module to a temporary file, opens it with
`theApp.OpenDocumentFile(Path, FALSE)` (which asks to save the open song;
no recent-files entry) and makes it untitled
(`CFamiTrackerDoc::MakeUntitled`), so Save asks for a name; the module's
comments show as it opens. A walkthrough is a tutorial with more step lines
(`TutorialScript.h`): `show: channel NAME` (the column, from
`CPatternEditor::GetChannelRect`) and `show: mixer`, `bar: N`, `play: bars
N-M` (a Play button in the note), `solo:` and `mute:` (the step's channels,
put back after it), `instrument: NN` and `wait: heard`. Channels are named
as the header names them, with the chip in front where two share a name
(`IsTutorialChannel`, `FindTutorialChannel`). The bars play through
`CSoundGen::SetNextPlayRange`: the next start halts before the first row past
them (or out of them, or back), with nothing read from that row, and
`PlayRangeEnded` says it got there; only a start that set it is affected.
A walkthrough ends when another song comes (`GetSongSerial`, counted in
`DeleteContents`). To add an example: compose it (check_song,
review_arrangement, measure_mix, check_loop), give it comments shown on
open (Module > Comments), put `name.ftm` and `name.txt` in `res/examples`,
add the RCDATA lines, IDs, a menu item with its prompt, a row of `EXAMPLES`
and the name to `TestExamples` in `Source/Tests/TutorialScriptTests.cpp`,
which reads every walkthrough and checks each bar and channel it names
against its song (a module of at most 24 KB, 8-24 bars, comments shown).
It also checks that the bars a step plays sound as in the whole song
(`CarriedIn`): FamiTracker starts each channel afresh where it starts
playing, so every channel's first note there names its instrument and
volume and sets again the lasting effects set before it (`Vxx` unless its
instrument has a duty sequence, `8xx`, `0xy`, `3xx`, `4xy`, `7xy`, `Pxx`,
`Axy`); the tools leave them out where nothing changes, so put them in.
It checks that every `instrument:` a step picks is in its song too, and
gives `sound-effects` (ten sounds at speed 1, a bar of 64 rows each, the
last halting with `C00`) 13 steps, one a sound. At speed 1 the sound
thread posts `WM_USER_PLAYER` 60 times a second, which under Wine kept
the note's timer from running until the bars had stopped, so it never
saw them play and `wait: heard` timed out: `CSoundGen::GetPlayStarts`
counts the starts that began playing, and the note takes a moved count as
its bars started (`Tick`). The note keeps out of the way of the instrument
editor while it's open (`OpenDialog`, `CMainFrame::GetOpenInstrumentEditor`)
as it does of a dialog, so its Play button stays in reach while the user
changes a sequence.

Stage fixes (from `feat-stage-fixes`): `review_arrangement` hears whether
the melody breathes (`BreathsOf` before `ReviewTool`: the two-bar spans
from `PhraseAnchor` with a rest of a beat or more or a note held two
beats, as it sounds through `HeardFrames`; under `BREATHING_SHARE` of
them, `CALM_BREATHING_SHARE` on a calm track, is a tip, and a busy part
guessed as the melody is left out). The calm mix keeps the
drums at the volumes they were written at unless they play louder than
`HeardPlaces::DrumsTop` under the melody (`PlacesFor`; the reply says why,
`DrumsWhy`; the lively mix sets them to their place, see Lively stages
below). `DrumPart` in `Arrange.cpp` has the `lively` groove, for gentle places
(`GROOVE_LIVELY`, appended; `LIVELY_SECOND_KICK`, `AddLivelyFill`, its hits
soft by design, which `volume` scales from `GrooveLoudest`), and light
thins the kick and the snare (`LastBackbeat`) and keeps the hi-hats. A part
on an N163 channel gets a sound of its own where another N163 channel plays
its sound there (`OwnWaveSound`, `OwnWaveSounds`, `N163SoundsBeside`: a
copy at a wave place of its own, `WaveSoundOfInstrument`, named like
"String pad 2", `CopyBase`), for voiced chords (the soft layer's pads in
turn), `MakeEcho`, `add_harmony` and `add_counter_melody`; `VoiceSound`
counts the soft layer's pads as one sound when telling voiced chords from
a melody. `ChordPartOptions::Lowest` keeps a pulse's broken chords over
`PULSE_BROKEN_FLOOR` (`ChordRegister` sets it). `read_notes` reads up to
`MAX_READ_BARS` bars or the bars of `MAX_READ_NOTES` notes and says first
which, and `vary_part`'s embellish works on notes of two sixteenths or
more. A key change into another mode (`transpose`, `change_mood`) goes
through `KeyMove`, which keeps a dominant's ninth natural (`MapPitches`
passes each note's row); `HarmonyWithoutRubs` moves `add_harmony`'s notes
off semitones against the other parts; `write_notes` keeps the volume of
the notes it replaces (`PartOptions::KeepVolume` in `BuildPart`) and the
bars its notes leave empty between them, writing the rest a piece at a
time; `vary_part`'s ending fits the chord under it. A held note breathes
in the span it starts in. `add_harmony`'s thirds and sixths are the key's
own: `ScaleStep` in `Arrange.cpp` steps over a harmonic minor's augmented
second, `HarmonyPart` gives a melody note outside the key the interval's
size that lands in it, and `ChordsForMelody` leaves the target's old notes
out (`HeardNotes`' `Also`). `HeardNotes`, `FindKey` and `SoundingChord`
take a note's `0xy` from the cells (`StickyAt`), since a `000` on a
release's row isn't in any note's effects. `SemitoneRub` is the one rule
for a semitone that rubs (and octaves over it), used by `check_song`'s
clashes, `EchoWithoutRubs` and `HarmonyWithoutRubs`. `TestMelodyBreaths`,
`TestStageMix`, `TestLivelyGroove`, `TestOwnWaveSounds`, `TestBrokenFloor`,
`TestReadNotesLimit`, `TestEmbellishShortNotes`, `TestDominantNinth`,
`TestHarmonyRubs`, `TestWriteNotesVolume`, `TestWriteNotesGaps`,
`TestEndingFitsChord`, `TestBreathSpans`, `TestHarmonyInKey` and
`TestEchoRubsAsChecked` cover them.

Chord helper (from `feat-chord-helper`): the piano roll's Chords button
(also H while the pattern editor shows, and a right-click on the chord
strip: `ChordsMenu` and `PutChords` in `PianoRoll.cpp`) puts a chord, or a
progression a chord a bar, on the channels with nothing sounding in its
bars. `Source/Api/ChordHelper.cpp` (portable, tested by its own
`chord_helper_tests`, `tests/ChordHelperTests.cpp`) works it out on a
`HelperSong`: `FreeChannels` (the channels `TakesVoice` allows, in the
order the kinds take voices), the melody and bass it keeps room for
(`RoomIn`, guessed as the tools guess them), the voices from
`VoicedChordParts` (or `ChordPart`'s arpeggio on one channel) and each
voice's sound (`ChooseSounds`: soft chords as each chip makes it, and on
the N163 a string pad of its own for each voice, copies placed in free wave
memory, since two channels on one instrument share its wave); `PlanCells`
writes the cells with `BuildPart`. `Source/PianoRollChords.cpp` reads the
track (`ReadChordSong`), makes the sounds and writes every voice with
`WriteChannelRows` between `BeginActionGroup` and `EndActionGroup`
(`WriteChordPlan`): one Ctrl+Z, though the instruments it made stay.
Its types share namespace `ftapi` with the tools' (its cells are
`VoiceCells`: a second `ftapi::ChannelCells` with another layout than
`Song.h`'s let the linker mix the two, which only MSVC's run showed), so
new portable code names its types apart.

Sound effects and cues (from `feat-sfx-fixes`): `CMixer::ClearBuffer` (every
`CAPU::Reset`, so every stop and start) takes the 2A03's pins as a reset
leaves them, the pulses, the noise and the DMC silent and the triangle
holding the step it stopped on, without sending a step; 0.4.6 started the
pins' sums from 0 but kept the channels' levels, so the first change after a
stop stepped by every level held (a held triangle thumped under the next
song's first noise, as loud at volume 1 as at 15; `TestHeldLevels` in
`ApuTests.cpp`). Sounds that play once ring out (`RingOut` in
`SoundEffects.cpp`: a layer stopping above volume 1 dies away, halving a
tick; not the triangle, which 0.4.6 plays whole at any volume, so a fade
would only hold it longer) and their tracks halt `SFX_SILENT_TICKS` rows after the last tick that
sounds (`SoundTrackRows`, used by `WriteSoundEffect`); the coin kind is a
glint of our own. `SoundLoudness` in `GameAudio.cpp` measures a sound
shorter than a 400 ms block over its whole length, ungated, for
`ExportTrack` and `measure_mix`. `UseOfTrack` (before `ReviewTool` in
`Tools.cpp`) tells music, a cue (it halts within `CUE_SECONDS`) and a sound
effect (speed 1, the 2A03 alone, at most a sound's length) apart: the
review's `Tip` keeps only the tips `CueTip` names (by their words, so a new
tip for stages stays out of cues; add a kept one's words there), a sound
effect gets `SoundEffectReview`, `measure_mix` `SoundMeasure`, and a song
that halts is measured to `HaltBar`. The roles, for every track: an echo
follows the melody's notes (`FollowDelay`: `EchoDelayOf` for four notes or
more, fewer each at one delay and about as long), a quiet arpeggio over the
melody is a line (`IsSparkle`, by `UsualDb`), and a low N163 voice a fifth
under the rest is the bass (`SoftBass`, in `PlaysBassPart`), in the review,
`MixRole`, `HeardRoles` and `measure_mix`'s parts. `TestSoundEffectEnds`
(every kind's last 50 ms as `RenderEffect` plays it), `TestShortLoudness`
and `TestCueJudging` in `Source/Api/tests/ApiTests.cpp` cover them.

The recorder's input (from `feat-recorder-input`): Edit > Start from a
groove (`Source/GrooveDlg.cpp`, also a button in Record from microphone)
writes a mood's drums, bass line and chords from the cursor's bar through
the `make_groove` tool (`MakeGroove` in `InsertRecording.cpp`, with
`CMcpConnection::RunTool`'s `FromDialog` set, the speed and tempo in the
same action group, and the key picked made the track's with `SetTrackKey`
unless it has a chosen key or notes found in another; `CGrooveDlg` starts
from the track's key, read with `CMcpConnection::Harmony`, in the mood's
mode: `ftapi::GrooveKey` gives its relative minor or major); the parts
themselves come from `ftapi::MakeGrooveParts` (`Source/Api/Grooves.h`,
implemented next to `MakeGrooveTool` in `Tools.cpp`: a `GrooveRequest` of
mood, key, bars as `BarSpan`s, tempo and channels in, a `GrooveParts` of
`PartNote`s and `DrumHit`s without instruments out), which the piano
roll's mood starters can call. In `RecordDlg.cpp` the song plays from a
bar before the take (`PreRoll`; at row 0 of a song that loops, its last
bar, `LoopsToStart`), the click's accents follow the song's bars
(`ClickTrack::barPhase`), a take over the selection (the pattern editor's
rows, or the piano roll's notes by their bars:
`CPianoRollWnd::SelectedBars`) replaces only those rows through
`CChannelNotes::TakeRows` and `ReplaceRows` (`NoteEdit.cpp`) and
`WriteChannelRows`, and stops by itself after them. Tapping the tempo is
`Source/TapButton.cpp` (`CTapButton` counts a tap on the button going
down, `IsTapKey` the T key) with `transcribe::TempoOfTaps` (`Tapping.cpp`)
and `ftapi::TappedSpeedTempo` (`Source/Api/RecorderInput.cpp`: the nearest
tempo with even rows within 3%, else `ChooseSpeedTempo` for the whole
BPM). Chords given to Add chords and bass line (`BackingDlg.cpp`,
`backing_choice_t::Chords`) are read by `ftapi::LayOutTypedChords` with
the tools' chord names (`Chords.cpp`) into `transcribe::Chord`s, which now
carry their notes and a bass (`tones`, `bass`, `ArpeggioTones`); strummed
ones (a take of the kind `RECORDING_CHORDS`, or a WAV file) are found by
`transcribe::HearStrums` (`Strums.cpp`: spectral-flux onsets, a
non-negative fit of each note's harmonics) and named bar by bar by
`ftapi::HearStrummedChords` (the tools' `HearChords`), once the delay
through the take is out (`TakeDelay`: where most strums agree they lie
past the beats, since an unmeasured microphone delay makes every strum
late). A take put into the song (not over a selection, not as a clip)
whose last note rings past the song's end ends that note on the last row
instead of adding a frame for it (`transcribe::EndInside` in
`Expression.cpp`, called by `InsertTake`), so a groove's song keeps its
length and its loop.

Phone takes (from `feat-phone-takes`): Edit > Record from your phone
(`ID_EDIT_RECORDPHONE`) opens `Source/PhoneDlg.cpp`, a modeless window with
a QR code of `PhonePageUrl` (this PC's home-network addresses, then its
name, `NetworkComputerName`, for Tailscale's MagicDNS) drawn from
`Source/Api/QrCode.cpp` (portable: byte mode, versions 1-40, levels L-H, the
penalty rules' mask; `tests/PhoneTests.cpp` compares it with codes
qrcodegen made, kept in `tests/QrReference.h`), and it offers
`CMcpConnection::AllowNetwork` while FamiTracker only listens to this PC. The
page is `res/phone.html` (RCDATA `IDR_PHONE_PAGE`, ASCII, nothing from
elsewhere): it records with `MediaRecorder` on a secure page or takes the
phone recorder's file (`<input type=file capture>`) elsewhere, decodes it
(`decodeAudioData`), mixes it to 16-bit mono WAV at 22050 Hz and posts it.
Its routes are `Source/Api/PhoneTakes.cpp` (`/phone`, `/phone/takes`,
`POST /phone/take`), which `HandleMcp` hands on after its peer and Host
checks (`McpConfig::pPhone`); they take `PhoneCode` (HMAC-SHA-256 of the
access code, so the code in a phone's history drives nothing else) and only
same-origin requests, and `CheckPhoneWav` and `IsIdeaName` check every take
before it's kept. `Source/PhoneLink.cpp` is the `PhoneHost`: it saves the
take in `RecordingsFolder` (never over a file) and, from a thread timer once
the answer has gone, makes it a clip with `GuessRecording` and
`InsertRecording` with a clip name (on `DefaultRecordingChannel(pDoc, -1)`,
the lead's channel), shows the clips panel and calls `SongChanged`. While the
main window is disabled, Claude Code is being answered
(`CMcpConnection::IsBusy`) or the mouse is captured, the take waits:
`PhoneTakes::Process` returns true and the timer tries again.
`TestPhoneRoute` in `ApiTests.cpp` covers the route through `HandleMcp`
and `place_clip` finding an idea by its name.

Studied sounds on N163 channels and the VRC6 Plus's soft layer (from
`feat-soft-references`): a study keeps an N163 sound's waves and wave
sequence (`nsf::StudySound::N163Waves` and `N163WaveSteps`, from the
module's instrument; `ReferenceSound`'s, `n163_waves` and
`n163_wave_sequence` in the reference's JSON, whole or not at all), and
`ReferenceWaveSound` in `References.cpp` makes any melodic sound a
`WaveSound`: an N163 sound's own waves (`N163WaveOf` in `Wavetables.cpp`
resamples each to 16 samples, one cycle of a wave drawn twice, each sample
the mean of what it covers), another chip's a wave from its channel
(`N163PulseWave` for each width its duty sequence steps through, the duty
sequence picking them; `N163TriangleWave`, `N163SawtoothWave`, an FDS
sound's wave), its volume 0-15 (an FDS sound's `FdsVolumeAs15`, a
triangle's full or silent as it plays) and its pitch sequences in the
N163's steps: CSequenceHandler adds them to the period, which on the N163
(and the FDS) is a frequency, so a 2A03 or VRC6 sound's turn around (alike
around A-3, 440 Hz, with four channels) and an FDS sound's go four times coarser.
`MakeReferenceInstrument` places it with `AddWaveInstrument` (under 128,
`FindWavePlace`), `SoftReferenceText` gives its level (`WaveSoundLevel`)
and what changed (an N163 sound whose wave's cycle, `N163CycleLength`,
isn't 16 samples sounded off its written notes where it came from, since
the importer writes the note of the frequency register: an octave under
for 32, `N163RegisterRemark`), and the sound index lists every studied
melodic sound for `TARGET_N163` and `TARGET_SOFT` (an N163 sound at home
there).
FakeSong's `AddN163Instrument` reports an N163 instrument's sequences as
`ReadSequences` in `McpConnection.cpp` does. `TestN163Study` in
`Source/Nsf/tests/FtmTests.cpp`, `TestSoftReferenceWaves` and
`TestSoftReferenceTool` cover them.

`measure_mix` in one pass (from `feat-soft-references`):
`SongHost::RenderParts` asks for several renders at once (the mix, the
melody alone, each part alone), which FamiTracker makes in one pass.
`CSoundGen::RenderToFile` takes them as stems (`render_stem_t`: a WAV file
and the channels left out, up to `MAX_RENDER_STEMS`), and `OnStartRender`
gives them to `CAPU::SetStems` once the APU is reset and the space cleared.
`CMixer::SetStems` makes each stem a mixer of its own as the main one is
then (`MakeStemOf`: settings, channels, and the synths exactly as they are,
`Blip_Synth::copy_state`, since `volume_unit` leaves a kernel attenuated
after a small volume; empty buffers, a silent space), leaves its channels
out as `ApplyChannelMix` does, and from then on the main mixer passes it
every call it gets, keeping them out (in the order a separate render gets
them: the 2A03's pins truncate each step). `CAPU::EndFrame` reads each stem
after the mix (`IAudioCallback::FlushStem`, `CSoundGen::FlushStem`), and
`CVRC7::EndFrame` makes each stem's samples from the channels' values
(`OPLL_calc_lr_parts`). `TestStemsInOnePass` in `Source/Tests/ApuTests.cpp`
finds each stem the same as the render of its part alone, sample for
sample, on every chip. `CMcpLink::RenderParts` renders 16 stems a pass and
reads each file back as the tool takes it (`CRenderFiles`); FakeSong's
counts `Passes`. `SoundMeasure` (a sound effect and each of its channels
alone) asks for its renders the same way. Under Wine, a 40-bar VRC6
Plus track with nine parts took 6.4-6.9 s of FamiTracker's CPU in one
pass against 13.3-16.5 s part by part (29-35 s against 62-75 s on a busy
machine), with the same reply. A short sound's renders depend on where the
chip was left (a reset keeps the triangle's step, as the chip does, and
the pins start there since `feat-sfx-fixes`, so a noise over it sounds
louder or softer): part by part, each render started where the one before
ended, and a 0.88 s explosion's parts came out up to 2 dB apart from one
call to the next; in one pass every part starts where its mix does, so
builds before and after differ there by that much. The whole sound still
moves by a few tenths of a dB from call to call.

Piano roll polish (from `feat-piano-polish`): the piano roll's Groove
button (and an empty song's "Start from a groove" box, `LayOutGrooveHint`)
opens `GrooveMenu` in `PianoRoll.cpp`, the moods of `ftapi::GrooveMoods`
written by `MakeGroove` over `GrooveSpan` (the chord helper's `ChordSpan` in
whole bars, else `GrooveBars` from the cursor's bar, kept in the "Recorder"
settings with the groove dialog's) with the melody left on the bright
channel (`GrooveMelody`); `groove_choice_t::Bpm` 0 keeps the song's speed
and tempo, which a song with notes does. More choices opens `CGrooveDlg`.
`Source/PianoRollUi.cpp` (portable, `pianoroll_ui_tests` in `Source/Tests`)
flows the toolbar into rows (`FlowRollButtons`; `LayOutButtons` measures the
buttons when a text changes, and the header is `HeaderRows` tall; a piano
roll too short for the rows and `MIN_GRID_HEIGHT` of notes keeps the rows
that fit, and the buttons with no room go under More, `MoreMenu`), writes the idle status hints (`RollHint`: E and H only while the
pattern editor shows) and tells a click that only closed a menu
(`ClosedAMenu`): a `WH_MSGFILTER` hook (`MenuClickHook`, installed in
`CPianoRollWnd::Create`) keeps the last button press a menu looked at
(`MSGF_MENU`), which Wine (and Windows at times) then passes on to the
window under the mouse; the button handlers ignore it, and a double-click
right after it is a click. MFC focuses the active view whenever the main
window is activated again (after any dialog or message box,
`CView::OnActivateView`), so `CPianoRollWnd::OnKillFocus` notes when the
keys leave the window from the piano roll and `CMainFrame::OnActivate` gives
them back (`TakeKeysBack`; not on `WA_CLICKACTIVE`, where the click decides),
and Ctrl+Z and Ctrl+Y pressed in the piano roll put the cursor back on its
channel after the action moved it (`RollChannelBefore` and
`RollChannelAfter` in `OnEditUndo` and `OnEditRedo`: undoing a groove or
another tool's change put it on the first channel written, and the piano
roll, following it, dropped its selection).
View > Piano roll's check mark, `NoteViews` and `GetPianoRollWnd` use
`CPianoRollBar::IsOpen` (set by `Opened`): `CControlBar::IsVisible` reported
the bar hidden in some layouts under Wine (the pattern editor hidden, the
drum grid closed).

Note volumes in the piano roll (from `feat-roll-volume`): Alt+Up and
Alt+Down make the selected notes (none: the one under the mouse, as E takes
it) a step louder or softer, Shift with them 4 steps, each note from its own
volume kept within 0-15 (`CChannelNotes::ChangeVolume` and `SteppedVolume`
in `NoteEdit.cpp`, writing the notes after them their old volume as
`SetVolume` does; `TestChangeVolume`), one undoable action per press
(`StepVolume`, `EDIT_VOLUME`); the status line says the volumes now
(`VolumeStepText` in `PianoRollUi.cpp`, `TestVolumeStepText`), and the
triangle's and the samples' notes are left alone with a word why. Ctrl+Up/Down
stay the instrument list's, Up/Down the transpose, and Alt and the arrows are
no note keys, so they work with the pattern editor hidden too (the pattern
editor's own Alt+Up/Down, `ID_CMD_STEP_UP`, no longer reach it from the
piano roll). Effects >
Volume has Louder and Softer (`FX_VOLUME_STEP`) over every volume 15-0
(`Soft (6)` keeps its name for the exercises' texts), the one all the
selected notes play at checked (`SelectedVolume`). The menu was for one
note only: a double-click's first click on a selected note narrowed the
selection to it (`MouseUp`, `DRAG_MOVE` without a move), so `DoubleClick`
puts back the selection that click narrowed (`ClickedFrom`, within the
double-click time). The `loud-soft` exercise and the glossary's
`volume-column` name the keys.

Melodies in steps (from `feat-step-notes`): `write_notes` takes `notes`
as text, a token a step of a grid (`D4 G4 A4 B4 - A4 G4 - | B4 - - - . . D5 -`:
a pitch starts a note, `-` holds it a step more, across `|` too, `.` rests),
and `read_notes` with `format` `steps` gives a part back so, a bar a line.
`Source/Api/StepNotes.cpp` (portable, tested by its own `step_notes_tests`)
reads and writes the text (`ParseStepLine`, `FormatStepLine`: bars, bar
numbers `5:` checked against where the bar falls, holds, rests, `B4v12`
volumes, drums of two words). `WriteNotesInSteps` and `ReadNotesInSteps`
(before `ReadChannel` in `Tools.cpp`) put it on the song's grid (`GridOf`:
an eighth is half a beat, a third of one where a beat's rows divide by three,
as `set_meter` makes 6/8), refuse a line whose bars don't each hold their
steps (naming every such bar with its count), and keep a note at the same
step and pitch as the song's as it was: its instrument, its effects (`Keep`),
the release or halt it ended with, the sticky effects it played with
(`KeepEffectsOn`) and their columns (`SameEffects`), so a line read and
written back gives the same cells (`TestStepsRoundTrip`). A `-` at the
line's start holds the note sounding into it (or ending at its first row, a
tie onto a line written before), so the write starts at that note's row, and
a kept last note that rang on past the line rings on (the write ends where it
did). The list of notes takes the old path untouched. `TestStepsWrite`,
`TestStepsRead`, `TestStepsMeters`, `TestStepsRoundTrip` and `TestStepsTaught`
in `Source/Api/tests/ApiTests.cpp` cover the tools.

Melodies grown from a hook (from `feat-grow-melody`): `grow_melody`
(`GrowMelodyTool` in `Tools.cpp`, after `AddCounterMelodyTool`) reads the
hook as a line of steps and asks `GrowMelody` (`Source/Api/GrowMelody.cpp`,
portable) for three versions over the chords `ChordsForMelody` hears (the
channel's own notes left out). A `Grower` plans the bars (`SectionPlan`:
blocks of four in the forms question and answer, hook and sequence, two-bar
calls; a calm song's phrases two bars long), gives each bar a rhythm (the
hook's own, its opening twice for a run, the kind's beat figures for climbs
and closes; `Tie` holds notes over inner bar lines), picks each section's
high point (`ChoosePeaks`) and the hook's moves up or down the scale
(`ChooseShifts`, `FitCost`; the hook stays as written where it fits as well
as where it's first stated), then chooses every pitch at once by dynamic
programming over the notes' candidates (`Choose`: chord tones on beats 1
and 3, the key's notes, nothing a semitone from another part for half a
beat, as `check_song` hears a clash, leaps paid by size and kind). A variant
seeds its own `GrowRandom`, so the same request gives the same versions on
every computer. `pick` writes one through `WriteNotesInSteps`, and
`GrowBreaths` counts the two-bar spans as `BreathsOf` does. It's in the
small set (`CoreTools`). `TestGrowPhrases`, `TestGrowShape`,
`TestGrowKinds`, `TestGrowTool` and `TestGrowTaught` cover it.

Sound groups (from `feat-sound-groups`): Instrument > Sound browser shows
its sounds in groups. `GroupSounds` in `Source/Api/SoundIndex.cpp` puts the
sounds found in groups (by kind; in each kind the ready-made sounds, the
reference library's game by game, `SoundEntry::Game`, then the song's own),
`KindCounts` counts them for the kinds list on the left, and `SoundRows`
lays out the list's rows with headings (`SoundView`: every kind or one,
headings folded by hand by `RowKey`, a kind's games folded while it has more
than `FOLD_GAMES_OVER` reference sounds and no words are asked for).
`SoundBrowserDlg.cpp` draws the headings by custom draw in its owner-data
list (no list-view groups: owner-data lists have none, and Wine's comctl32
draws them only in part; a heading is drawn as a row, then over it in
`CDDS_ITEMPOSTPAINT`, since Wine puts the focus rectangle where it last drew
the focused row itself), takes every click itself (`CSoundList`: a sound
plays, the one picked too; a heading folds), lays itself out as it's resized
(`Layout`) and stops the sound in `OnCancel`: under Wine without a window
manager the UI thread waits a second or two after any modal dialog closes,
before the dialog's `OnDestroy` runs. `PreviewSteps` plays a phrase for each kind (`PHRASES`,
`DrumSteps`). `Source/SoundPreview.cpp` (`CSoundPreview`, timed by a
message-only window) plays the previews and the note keys for the browser
and for Instrument > Listen (`ID_INSTRUMENT_LISTEN`, the speaker button after
Edit under the instrument list: `ListenToInstrument`, which lists the song's
own with `ToolSet::Sounds`' `Library` false). `TestSoundGroups`,
`TestSoundGroupsQuick`, `TestPreviewPhrases` and `TestSoundsWithoutLibrary`
cover them.
What next (from `feat-what-next`): `what_next` (`WhatNextTool` and the
`WhatNext` class at the end of `Tools.cpp`'s anonymous namespace; its
portable pieces in `Source/Api/WhatNext.cpp`) gives the one next step for
the selected track with its call, worked out from the song alone (notes,
sections, key, loop, the game's file next to it), in the recipes' order:
`check_song`'s findings with a fix (`Breakers`: any finding that carries a
`fix` is a step of its kind's name, its `then` calls in the why; severity
high at once, medium after the parts (`PartFixes`), low with the tips), the
frame (`Frame`), the parts and
the form (`Parts`: `SongForm` reads the sections, `NameSections` names
unnamed bars by their chords, `MelodyStep` gives `grow_melody` the song's
hook (`SongHook`) or one offered on the chords (`OfferedHook`, from
`ExampleTune`), the first that grows without a clash (`TryWrite`), else
`ExampleTune`'s phrase as `write_notes`, `GrowForm` grows A, A again, B on
`NextProgression`'s chords, A),
depth (`Depth`: `add_harmony` in the repeat, a `vary_part` that adds no
clash, a break and an intro, the loop), the review's tips (`TipStep` matches
their words: keep them in step with `ReviewTool`'s), the echo last (it
copies the melody), `Polish`, `Mix`, the seam (from the first of the three
that's due, one `finish_song` step: `FinishStep`), saving and the export.
Whether a tool would change the song is a dry run (`DryRunHost`,
`WouldChange`, `DryChange` in dB): a mix that would move no note more than
1.5 dB is done (mix rounds), and polish once no part plays at one volume and
its other touches change nothing (its accents re-accent a part left with two
volumes, a step softer each time). `DryRunHost::Overlay` reads the cells as
the writes kept back would leave them, so `TryWrite` hears a variation's
or a grown melody's clashes before it's given. `export_for_game` writes `FAMITRACKER_PRINT`
(`SoundPrint`) and `FAMITRACKER_LOUDNESS` into each OGG's comments
(`ReadOggComments` in `Ogg.cpp` reads them back). The small set
(`CoreTools`) is `what_next` and every tool its steps call and `explain`, 38 (with `song_shape` and `listen`), not
`set_mixer`; `CoreInstructions` and `CoreRecipe` are built around the loop,
the recipe with a worked melody of 8 bars in steps (First song's, from
`res/examples`). `TestWhatNextLoop` follows it from an empty song to done
for each kind and on the small set (each call run as given, each answer
new, the result clean), and `TestWhatNextStates`, `TestWhatNextChoices` and
`TestWhatNextCore` cover the states, kinds, skips, the phrase, the dry run
and the small set.

Checks for drafts and short replies (from `feat-checks-short`): what a
local model's first stage missed while every other check was clean is
`ArrangeFindings` (before `ReviewTool` in `Tools.cpp`): `PhrasesFinding`
(a melody that stops at most bar lines, `StopsAtBarLines`), `BassFindings`
(the bass, `MainBass`, thin as `HearLine` hears it, or another channel
doubling its roots now and then, `DoublingShare`), `SoundFindings` (a soft
sound on the triangle, odd duties on the sawtooth), `FadersFinding`,
`RepeatFindings` (named sections compared bar by bar, `BarsAlike`) and
`LoopFinding` (style lively only, `SHORT_LOOP_SECONDS`); none for cues,
sound effects or songs at speed 1. Each `ArrangeFinding` carries its fix as
a call (`ToolCall`, the calls after it in `then` with `ThenCall`, written
out by `FixText`), and its constants say what they were measured on.
`check_song` reports them as the kinds `phrases`, `bass`, `sound`,
`faders`, `repeat` and `short_loop` (after `held` in `CHECK_KIND`, and in
`KIND_IDS`, `problem_kind` and the panel's `WORDS`), every one listed; the
problems panel's Fix (`ACTION_FIX`) makes a fix of one call
(`ProblemFinding::FixCalls`), and Explain gives the others' calls. The
review weighs them into its score and tips (`ReviewTip`), and `BreathsOf`
counts a rest before a bar line as a breath only where it ends a phrase of
two bars or more (`MelodyBreaths::PhraseEnds` for the finding). `mix`
takes `faders` `reset` (the faders to 0 dB in the mix's action group; calm
and lively give the melody, through `levels`, the level its fader played
it at, less what the others' faders lifted them past their volumes' room), and
`vary_part`'s embellish leaves out an ornament that would rub a semitone
against another part (`SemitoneRub`; `TestEmbellishWithoutRubs`).
`review_arrangement`, `check_song` and `measure_mix` take `format` `short`
(`ShortReview`, `ShortTip`, `ShortFinding`, `ShortMeasure`), the default
for `Caller::Core` in `ToolSet::Call`. `TestDraftFindings`,
`TestDraftFixes`, `TestFindingsQuiet`, `TestShortReplies`,
`TestMixFadersReset` and `TestProblemListArrangement` cover them, on
`DraftSong`, a small copy of that stage.

Warnings as a part is written, and the last steps in one call (from
`feat-finish-guard`): `WritingWarnings` (with `FinishSongTool`, after
`ExportTool` in `Tools.cpp`; declared before `WriteNotesInSteps`) gives
`write_notes`' reply its "Warning:" lines, both paths and `grow_melody`'s
`pick`, from the channel's cells as the write leaves them (a copy of the
session hears the song so, `Session::Assume`), before `Commit`: the range
`check_song` checks (`NoteRangeReason`, which both use now, with
`N163HighestWhole`), a bass sounding under `GUARD_LOWEST`, a line on a sound
that holds whose middle reaches `GUARD_LEAD_MIDDLE` and top `GUARD_LEAD_TOP`,
notes outside the key (`PitchOutsideKey`, check_song's too) that no other
part plays in that bar, that make no major or minor triad with the notes the
others sound with them (`ChordedOutsideKey`: a borrowed chord's root in the
bass under its other notes) and that aren't short chromatic steps (only a chosen key's,
or a third of the part outside a key found from the notes), a bass of
lone short notes over four bars from its first note, and a sound the channel
doesn't play as meant (`TRIANGLE_SOFT_PEAK`, another chip's, `OddDutyRemark`).
Each fix is a call (`GuardCall`, `CallText`): `transpose` on the full set, the
line moved an octave (`StepLineMoved`) on the small set (`WriteNotes` takes
`Caller::Core`). On the soundtrack's 56 finished tracks and its cues, written
back section by section, they ask one key question and repeat check_song's
N163 range findings in one track (the newest tracks' drafts, none); widen them
only against that. `finish_song`
(`FinishSongTool`, a `FinishKind` of `FINISH_KINDS` naming what_next's kind,
whose `NextKindInfo` gives the mix's style and the loudness, and whose polish
arguments are `KindPolish`'s, shared with what_next's polish step; keep the
kinds' numbers in the tools' styles, not here) runs `PolishTool`, `MixTool`
(with `faders` `reset` when
`MELODY_DOWN` or `FADER_LIFT` says the faders do the mix's work),
`FixLoopTool` where `FindSeams` finds a seam to write and `ExportTrack`, each
on a new `Session`, the first three inside `SongHost::BeginGroup`/`EndGroup`;
`FinishBalance` hears the balance after the mix as the review does.
`what_next` gives `finish_song` for the last steps (`WhatNext::FinishStep`,
from the first of polish, the mix and the seam that's due: polish skipped once
the song is mixed and the mix when only the seam is left, as the call's own
skips; the file alone stays `export_for_game`, and the save step after a
finish says the file has the changes), and the small set has it.
`TestWritingWarnings`, `TestFinishSong` (the separate calls give the same
notes, mixer and file; one undo) and `TestWhatNextFinish` cover them.

Sounds, sound effects and the 5B (from `feat-fixes-sounds`):
`make_sound_effect`'s loops last as long as asked within the song
(`MaxLoopTicks`: its frames' rows, a row a tick). A layer longer than an
instrument's 251 ticks plays as pieces (`SoundPieces` in
`SoundEffects.cpp`), an instrument each with a note where it starts, alike
pieces sharing one (a loop played over and over is cut in whole turns that
divide it, `LoopOfLength`'s `Period`) and a silent piece left out;
`WriteSoundEffect` refuses a sound needing more instruments than the song
has room for. The pulse writes its period's high byte only when it changes
and the noise its registers every tick, so the pieces join without a seam.
Wind, rain and fire are drawn at the length asked (`MakeLong`: `WindFor`,
`RainFor`, `FireFor`), the other loops repeat; `ChangeSoundEffect` no
longer cuts a layer at 252 ticks (`FitSoundEffect` keeps a sound in its
limit), and `UseOfTrack` takes a looping 2A03 track at speed 1 and the
default tempo as a sound however long. `design_instrument` reads `words`
(`ReadDesignWords`, `ApplyDesignWords`, `DesignWordsText`) where the kind's
defaults stand, the arguments over them, and on the 5B makes the chip's
noise at a pitch (`noise`, `noise_pitch`: `Preset5BSequences` and a mode
sequence of `S5B_NOISE` and the pitch; `DescribeDesign`'s wave "5B noise").
A 5B note that plays the noise alone (`PlaysNoiseOnly`: its mode sequence,
or `V02` for an instrument without one) has no pitch heard: `PitchedNotes`
leaves it out of `FindKey`, `GuessMelodyChannel`, `HeardNotes` and
`check_song`'s key, range and clash checks. `measure_mix` without a melody
(none playing, `melody_channel` none, or the guess heard for under a fifth
of its bars and playing under a note and a half a bar there: `Presence`,
`MELODY_HEARD_SHARE`, `MELODY_NOTES_A_BAR`, measured on the soundtrack)
measures each part against the whole and says why, and `check_song` checks
no key there unless one is chosen or given (`CarriesTune`). `make_jingle` takes its
key, lead and sounds from the music (`FindJingleMusic`: the nearest music
track when the selected one is a sound effect; never an effect's
instruments). A studied sound whose release ends above 0 droned on after a
release: `FadedRelease` in `References.cpp` fades it out, halving a frame,
in `ReferencePreset`, `ReferenceFdsSound` and `ReferenceWaveSound` (so the
sound index too), and `reference_sound` says so (`ReleaseFadeFrames`). In
the BPM layout a song at speed 1 shows the frames a row lasts under the
label Speed (`LayoutShowsFrames`, `LayoutFramesText` in `RecorderInput.cpp`;
`CMainFrame::BPMFrames` and `ShowBPMText`, the box starting just after its
label in `LayoutSongSettings` so "1 frame" fits), and 1, a BPM over 400 or a
step past it sets speed 1 at the default tempo (`LayoutWantsFrames`,
`CMainFrame::SetBPM`, `OnDeltaposTempoSpin`; a BPM set from speed 1 picks
its speed from 6, since the chooser keeps the current speed when it can). `TestSfxLongLoops`, `TestS5BNoiseDesign`,
`TestS5BNoiseChecks`, `TestMeasureAmbience`, `TestJingleAfterEffect`,
`TestReferenceReleaseEnds`, `TestDesignWords` and `TestBpmLayoutFrames`
cover them.

The sound library (from `feat-sound-library`): 234 ready-made sounds of
our own beside the presets, in `SoundLibrary.cpp` (`LibraryPresets`,
`LibraryVrc7Sounds`), `Wavetables.cpp`'s `LibraryWaveSounds`,
`FdsSounds.cpp`'s `LibraryFdsSounds` and `DpcmKit.cpp`'s `DrumKits`, which
the Find functions look up after their own, so a new name must differ from
the old names and their aliases; `TestSoundLibrary` checks each against the
vanish check, pulse clicks, the games' sequences and every other sound
listed with it (none alike). `GameSounds.cpp` carries the sounds of the 12
soundtracks the user made public (`GameSoundsData.h`, written by
`tools/game_sounds.py` from the studies; it reads only the soundtracks in
its `PUBLIC` list, so add one only when the user makes it public, and
`TestGameSounds` checks every built-in one is there): each sound once a
game, with the tracks it plays in (`SoundEntry::Builtin`), the library's
copies shown as the game's (`SameGameSound`, `GameSoundKey`), and
`reference_sound` takes a built-in track's id without a library
(`BuiltinReference`). `SoundListing::OtherChips` lists the sounds of chips
the song doesn't have (`ChipsWith`, `SoundEntry::AddsChip`), never one
whose chip would clear notes; the browser asks `AddChipQuestion`'s
question before `AddChipForSound` adds the chip as Module properties does
(which clears the undo history), and `find_sound`'s `other_chips` gives
the `set_expansion` call to make first. `TestOtherChips` covers them.

Lively stages (from `feat-lively-fix`): the night songs' rules are for calm
tracks only; a lively stage is made the way the soundtrack's first lively
stages were (their numbers, measured as heard, are in the comment before
`PlacesFor`). `HeardPlaces::Band` marks lively's places: `MixTool` lets the
melody through the soft branch (`SOFT`'s 9, as on a pulse, scaled as
written so accents and steps by section stay; `MelodyShift` follows it),
each chord layer moves on its own so the chords together
(`ChordsRel`) take `Places.Chords` (`LivelyChordShifts`, see Arranging fixes
for the later tracks below), no nearer the melody than
`LIVELY_CHORDS_TOP` in any part of the song (`HeardBalance::Parts`, each
named section or four bars; the reply's `ChordsWhy`), a part within `BAND_SLACK` of its
place moves only with the melody and the search keeps a part's volumes
unless another lands `BAND_SLACK` nearer (the parts sit low, where a step
is 2-3 dB: without it a second mix rounded them anew and what_next asked
for the mix again), and the drums move up as well as
down (`Room`, the search's `Top`, the DPCM compensation; placed by the
noise's hi-hats since `feat-fixes-arrange`; a triangle under
DPCM samples comes down no further than Z40, so it can stay a little over
the melody, as the first stages' did). `HeardRoles` with `Band` keeps
lines that move with the tune (`MovesWithTune`: a harmony voice, a double
an octave under) as lines rather than a pad's voices; calm keeps the old
rule. `HearBalance` also gives `ChordsRel`, `ChordParts` and
`BandRel` (every channel but the melody's), which `BalanceText` reports
with `Band` (and `FinishBalance` for finish_song's lively mix). The review's lively tips are the band (`LIVELY_BASS_UNDER`),
the lead at one volume ("Flat dynamics on the lead") and the lift
(`LiftOf`, `SongLift`, `LIFT_LEAST`: the loudest named section, intro,
break and outro aside, against the first A, or 4-bar blocks); it gives no
held-lead or "presses" tip on a lively track, and neither does
`check_song`. `polish` style lively gives the default touches (accents).
`measure_mix` style lively checks the chords together (3-4 dB under the
melody), the bass (about level: 1 under to 2 over) and the echo (6-8
under) (`MixPart::Role`) and renders the band as a stem of its own. `mix`
says when `width`, `levels` or `dac` without a style changed only those
(`NoStyle`). `what_next`'s lively kind (`KINDS` in `WhatNext.cpp`) designs
a `bloom lead` with `restrike_beats` 0, lush chords of `LushFrames` (2), the
basic groove with kit samples, polish with its accents (`KindPolish`,
finish_song's too), turns the
review's lift tip into a harmony in the last part (`WhatNext::Climax`), and
takes a lively mix as done while another would move no note more than 2 dB
(`MixDue`; 1.5 for the other kinds), since a step at its parts' volumes is
up to 2 dB.
The `lively` groove's aliases no longer include stage words (`basic` has
them). `TestLivelyBand`, `TestLivelyPolish`, `TestLivelyChecks`,
`TestLivelyRecipes`, `TestLivelyWhatNext`, `TestMixWithoutStyle` and
`TestLivelyChordsTop` cover them; calm's results are the old ones (its
tests unchanged).

Arranging fixes for the later tracks (from `feat-fixes-arrange`):
`add_harmony` over a chord from outside the key takes the scale that chord
sounds in (`ChordScale`, `InChordScale`, `ChordScaleStep` and `OutsideKey`
in `Chords.cpp`, used by `HarmonyPart`'s `Plain` and `HarmonyWithoutRubs`),
and of the chords the other parts play keeps the notes outside the key only
where a part plays them (`SoundingTones`: `HearChords` named an E bass under
C and E an E augmented). `add_chords_and_bass` over a bar or two takes the
song's melody and register (`AddBackingTool`'s `Tune`, `UsualLowOf`,
`BELOW_THE_TUNE`), gives bars without the melody their own register
(`ChordPartOptions::Open`, `RoomFor`), keeps lush chords' roots on a pulse
over C-2 (`ChordRegister`, `RoomyPitch`'s `Least`), voices chords an octave
under the tune (`VOICES_UNDER`) with the root at the bottom where no bass
holds it (`VoicedChordParts`' `Rooted`), and keeps walking and syncopated
basses' passing notes off the chords' and the other parts' (`BassOptions::Above`,
`BassRubs`, `CleanWalk`). `add_counter_melody` style calm holds a note a bar
(`CounterOptions::Slow`) and says when there's no rest to answer in;
`make_groove` makes its key the track's (`Commit`'s key) unless one is
chosen or the notes say another. Roles: a triangle that thumps now and then
isn't the bass beside a sawtooth's line (`TriangleCarriesBass`,
`HeardRowsIn`), voiced chords are the chords beside a quiet arpeggio
(`ChordVoicesIn`'s `Every`; a channel whose notes sound chords is no voice,
nor an N163 bass), and `measure_mix` gives the chords together, the arpeggio
and the voices. A calm balance in layers gives the chords together too, and
its "presses" tip judges them together (`BalanceText`; Soft and Space moved
the stems by tenths of a dB, the layers by 2.5). The lively mix's chord
layers each move on their own (`LivelyChordShifts`), the review tips chords
that cover the melody in one part, `LIFT_LEAST` is 1.5, and the drums are
set by the noise's hi-hats alone (`HatsAgainst`, `IsHatSound`: `LIVELY_HATS`
14 dB under, a noise without hats `LIVELY_DRUMS` 11.5, left as written
within `DRUMS_SLACK`, the search reaching down to where the shift took
them). `TestHarmonyChordScale`, `TestBackingRegisters`,
`TestBassPassingNotes`, `TestLushFloor`, `TestCalmCounterRoom`,
`TestGrooveTrackKey`, `TestPartRoles`, `TestWhatNextSectionsDone`,
`TestLivelyChordLayers`, `TestLivelyLift`, `TestLivelyHats` and
`TestCalmChordLayers` cover them.

Editing fixes (from `feat-fixes-edit`): `set_swing` reads a swing's middle
where the rows before play its short unit's speed (`SwingBefore` and
`MeantSpeedBefore` before `SetSwingTool`: speed commands on every half or
quarter beat alternating between two speeds), for the speed a range swings
around and the one the rows after it play at, so "none" leaves no `F05`
behind; `ShiftSong` (insert_bars) swings the bars it puts in after a swing
(`SwingNewRows`). `FreeSectionName` in `Sections.cpp` numbers a copy the way
the song numbers that name (`NumberedRoot`: "A3" beside "A2") and never
takes one that reads as another's (`SectionNameTaken`: "A 2" beside "A2").
`write_notes`, both paths, gives a note that names no instrument the one the
channel plays at its place (`InstrumentAtRow`), written where it changes and
on each piece's first note, and says which (`ChosenInstrumentsText`); on the
noise and DPCM channels `instrument` can be an object of kinds
(`DrumSoundsArg`, `DrumHitInstrument`; one instrument for a noise line leaves
the open hi-hats their own unless it rings as long, `RingsLikeOpenHat`); a
note running past the song's end ends with it unless notes start past it or
`to_bar` asks for more, and growth is said (`GrowthText`). A line of steps
joins two words into one drum only where they make one's name (`DrumPair` in
`StepNotes.cpp`), so hits in a row ("hihat hihat") stay steps of their own
(`TestDrumRuns` in `step_notes_tests`). `polish`'s
`Accents` returns -2 for a part accented already (each bar's notes the steps
under its loudest), so polishing again changes nothing; `StaccatoBass` cuts
only where `PlaysBassPart` hears the bass (section by section, `PolishSpans`)
and not in bars without the melody. `mix`'s `WriteDac` over a range sets the
old level back on the row after it. `FindSeams` gives a sound that fades on a
release `SEAM_RELEASE` (fix_loop writes `===`), and voiced chords release
their last chord on the song's last row (`PartOptions::EndWithSong` in
`BuildPart`). Undo says what it undoes: `SongHost::UndoSerial` (the pushes,
`CMainFrame::GetActionSerial`) and `LabelUndo` (`CAction::SetLabel` on
`CMainFrame::GetUndoAction`); `ToolSet::AfterCall` labels each call's change
(`ChangeLabel`), takes back what a call that failed had changed, and prefixes
undo's reply after a failed call (`m_FailedCall`); the hosts' `Undo` name the
label, or an edit made in FamiTracker. The bloom lead holds by itself
(`DesignOfKind`), words that ask restrike every half beat
(`DesignWords::Restrike`), `RestrikeText` says when a sound restrikes, and
`what_next`'s action lead passes `restrike_beats` 0.5 to keep its sound. The
calm recipe (`KindRecipe`) and the style guide's `calm` topic lead with the
night songs' best: the music box's shape on a VRC6 pulse at 37.5%, its
texture, an echo from the second A, an event in the break and a tune of its
own. `TestSwingReadsItsMiddle`, `TestCopyNamesApart`, `TestWriteNotesSounds`,
`TestPolishTwice`, `TestMixDacRange`, `TestLoopReleasesPads`,
`TestUndoSaysWhat`, `TestBloomHolds` and `TestCalmRecipeLeads` cover them.

Browser and tool fixes (from `feat-fixes-browser`): `ToolSet::Call` refuses
a call with an argument its tool's schema lacks, doing nothing
(`ToolSet::UnknownArguments`, public so the tests check calls without
running them: `NearestArgument` names the one likely meant); the tests'
`UnknownInText` reads the instructions, recipes, prompts, descriptions and
`what_next`'s replies for `tool {json}` and "tool with argument", and
`CheckFixCalls` check_song's fixes, so keep every call written anywhere in
step with the schemas. The browser's Enter picks `BestSound` (the shown
sound `SoundMatch` scores best, one that plays here before another chip's,
then list order; `GroupSounds` keeps a kind's ready-made sounds first, so
the best can sit lower) after `OpenTo` opens its headings, the
kinds list is `KindsListed` (the browser keeps the kind picked by its index,
`m_iKind`, `g_Kind`, not by row), and every preview, note key and Listen
plays at `SoundEntry::Volume` (`PreviewVolume` beside `ToolSet::Sounds`:
`VolumeForDb` at 0 dB, the fader left out; `CSoundPreview` writes it in each
note, which had none: the channel's own, full after a stop).
`CSoundGen::StopRendering` clears the buffer before `MakeSilent`: 0.4.6 reset
the APU after the channels' reset, which left `$4015` off, so the 2A03 was
silent for previews and note keys after every render until a song played.
`ShortTitle` in `References.cpp` makes a studied sound's name (before a
parenthesis, bracket or tildes; a long one to its first clause or its
start's words within 20 letters; an ending number kept), and the sound
index's `Lister::Unique` numbers alike names across studies and the
built-in games (`GameSoundsData.h` holds titles, not names). The status bar
shows a song at speed 1 as `StatusFramesText` (`RecorderInput.cpp`,
`CMainFrame::OnUpdateSBTempo` with `CSoundGen::GetPlayerSpeed`; the pane is
widened for `STATUS_FRAMES_LONGEST`). `TestUnknownArguments`,
`TestBrowserFixes`, `TestStudiedNames` and `TestStatusBarFrames` cover them.

Follow-up fixes (from `feat-fixes-followup`): `what_next`'s calm kind
(`KINDS` in `WhatNext.cpp`) designs the night songs' music box (`CalmLeadArgs`:
`attack` bloom, `duty` 37.5% on a VRC6 pulse, 50% on the 2A03's), as do its
"presses" tip and `CoreRecipe`'s calm steps, the breath second. The sound
library's bloom lead holds as `DesignOfKind`'s does (its soft vibrato kept,
which the library's click test needs). `PaceText` before `SongOverview` says
a tempo as the status bar does ("1 frame a row" at speed 1) for get_song,
`TempoText`, `VolumeTimeline`, the review's length and what_next's tempo.
`ToolSet::AfterCall`'s rollback calls `SongHost::ForgetRedo`
(`CMainFrame::ForgetRedo`, `CActionHandler::ClearRedo`; FakeSong counts
`Redoable`). `PlanCells` in `ChordHelper.cpp` ends a voice on the last row
that plays (`SongFlow::EndRow`), where it ended a row too late when a jump
left the last pattern early; it already released its voices at an ordinary
song's end. A calm mix moves each layer group (`HeardBalance::GroupRel`,
measured only for groups of two layers or more, `GroupTogetherShifted`) so
the chords together take `Places.Chords`. `ChordVoicesIn` counts notes a
row apart as starting together. `AddBackingTool`'s fitting hears
`OtherLines` at `OTHER_LINES_WEIGHT`, `SecondaryDominants` (split at bar
lines, then joined) makes a span whose heard note from outside the key is
the third of the next chord's dominant that dominant (58's A7), and
`BassOffRubs` in `Arrange.cpp` moves a fitted chord's bass note off another
part's semitone where none of that helps (`RubsAgainstParts`, the part of
`BassRubs` that hears the other parts). `TestCalmLeadFirst`, `TestBloomLeadAgrees`,
`TestPaceAtSpeedOne`, `TestRollbackForgetsRedo`, `TestCalmLayersTogether`,
`TestPlucksRowApart`, `TestFittedChordsHearLines` and `TestEndWhereItGoesBack`
(in `chord_helper_tests`) cover them.

Chords with roles and strong notes on the beat (from `feat-roll-helpers`):
`Source/Api/ChordRoles.cpp` (portable, its own `chord_roles_tests`) gives a
chord's role in a key with `RoleOfChord`: home (the tonic and the chords
sharing most of its notes), away, tension or color (borrowed from the
major or minor key on the same tonic, the key's own notes added to it
allowed: Dbmaj7 in Eb major; a chord leading to another of the key's; or
a surprise), `chord_role_t`, with a label and a feel word
(`ChordRoleInKey::Words`: "tension: leans toward home"). Every mode of
`MODES` has its own table (`InKey`, by the root's semitones over the
tonic; minor and harmonic minor share both sevenths), a major chord on
the fifth and the leading tone's diminished one are tension in any mode,
and a chord moving off over the fifth in the bass (F/G) is a soft tension.
The chords right before and after decide "back home" (the tonic after
tension), a surprise after the dominant (V vi, V VI in minor) and whether a
chord both borrowed and leading on (A or D in A minor) leads on.
`RolesOfChords` takes chords in time order, `ChordRoleName` and
`ChordRoleMeaning` are the legend's words and `ChordRoleExplained` a
sentence on each (the glossary's and the tools' to share), and
`ChordIndexAtRow` and `StrongNoteAt` give the chord tones under a row.
`ToolSet::Harmony` fills `HeardChord::Role`, `RoleWords` and `Bass`, so
the roles are worked out with the chords, a moment after a change. The
piano roll colors the chord strip by them (`ROLE_COLORS`, Okabe and Ito's
colors, with `RoleMark`'s house, arrow, bolt and diamond so color isn't
all), draws a legend at the status line's right (`DrawLegend`) and says the
words when a chord is pointed at (`ChordText`); with strong notes on,
`LitChord` lights the lanes and keys of the chord at `StrongRow`
(`StrongNotesRow` in `PianoRollUi.cpp`: a note being placed, the mouse over
the notes with Draw or Paint, else the cursor), a chord pointed at in the
strip first. The two toggles end the Chords and Key menus
(`AddHelperToggles`) and are kept for each profile in the "Piano roll"
settings ("Chord roles" and "Strong notes" for Everything, with ", game
music" for that profile, where they start on: `ProfileIndex`).
`LetGoOfMouse` (the timer) puts out what the mouse lit once it leaves the
window. `TestHarmonyRoles` in `ApiTests.cpp` and `TestStrongNotesRow` in
`PianoRollUiTests.cpp` cover the rest.
The glossary (from `feat-glossary`): `Source/Api/Glossary.cpp` (portable,
ASCII, tested by its own `glossary_tests`, `tests/GlossaryTests.cpp`) holds
the entries (`GlossaryEntries`: an id, a term, its other names, a category,
two to four sentences, see-also ids, a Hear it example's id or `HearHow`),
finds one by any name (`FindGlossaryEntry`: an effect as written, `447`, too),
searches them (`SearchGlossary`) and writes the `explain` tool's text
(`GlossaryText`; `ExplainTool` after `RecipeTool` in `Tools.cpp`, needing no
song, in `CoreTools`). The effects' entries are built from `Effects.cpp`'s
table, which gives each effect a newcomer's text (`EffectInfo::Plain`) and a
value to hear (`Example`), the song's own effects a table of their own
(`SongEffectReference`), and `DescribeEffect` says an effect's own numbers in
words. Hear it is a score (`HearExample`: events in ms on voices, a channel
kind each, `HEAR_HERE` the hovered effect's channel; `MakeExample`, and
`EffectHearExample` for an effect as written); `GlossaryLabels` names the
entry of each label FamiTracker explains, so the tests check them, and its
`Also` the entry a tooltip names too (the piano roll's Key and Chords: the
chords' roles). What chords do in a key (`chord-home`, `chord-away`,
`chord-tension`, `chord-color` and the `chord-roles` they hang from) is said
in `ChordRoles.h`'s own words (`ChordRoleExplained`, `ChordRoleMeaning`), as
the piano roll's chord strip says it, and the song's parts (intro, A and A',
B, bridge, climax, break, outro, verse, chorus) in Song shapes' (`PartLine`,
by `AddSongParts`); the song's form names Module > Song shape and its shapes.
`Source/GlossaryDlg.cpp` is Help > Glossary (`ID_HELP_GLOSSARY`, Ctrl+Shift+G,
the toolbar's last button, the 26th image of both toolbar bitmaps), a
modeless tool window shown with `SW_SHOWNOACTIVATE` so it never takes the
note keys, its place and whether it's open in the "Glossary" registry
section (`RestoreGlossary` from the tips' first tick once the main window
shows). `Source/GlossaryHear.cpp` plays an example as the note keys play
notes (`QueueNote`, at most one event per channel per frame), each channel
on a plain instrument of its chip: the song's own one without sequences, or
one added quietly for the example (no instrument-list update, the modified
flag put back right after the add and the remove) and removed with
`RemoveInstrumentAndSequences` when it ends, only in the same song
(`GetSongSerial`); `SilentAll` then resets the channels unless the song
plays. `Source/GlossaryTips.cpp` is one tracking tooltip polled every 100 ms:
the pattern editor's effects (`CPatternEditor::GetCellAtPoint`), the song
settings' labels (subclassed with `SS_NOTIFY` as `CGlossaryLabel`, a click
opens the glossary there), the instrument editor's `IDC_INSTSETTINGS` list,
and windows that draw their own parts through `AddGlossaryTipArea` (the
mixer's Soft, Space and pan in `CMixerPanelWnd::Create`, the piano roll's Key,
Snap to key, Chords and Groove in `CPianoRollWnd::Create`); it writes the
status bar's line for the effect under the cursor. F1 (`ID_HELP`, now
`CMainFrame::OnHelpContext`) opens the entry a tip shows, or the cursor's
effect while the pattern editor has the keys, else FamiTracker's help, and
Help > Hear the effect at the cursor (`ID_HELP_HEAREFFECT`, Ctrl+Shift+H)
plays the tip's effect or the cursor's, plain and then with its numbers,
with the note and instrument it plays there (`HearEffectAtCursor`). An
effect the note's instrument overrides (`Vxx` under a duty, wave or noise
and mode sequence: `EffectOverriddenBy`) says so in its tip and is heard
on a plain sound of the chip instead.

Song shapes (from `feat-song-shapes`): `Source/Api/SongShapes.cpp`
(portable) holds six shapes (`SongShapes`: stage, night, town, boss, small,
jingle), each a list of parts with their bars and a line on what the part
is for, the forms the recipes teach; `PartLine` gives a section's line from
its name (a shape's own where it's one of its parts), `MatchSongShape` the
shape a track's sections are (their names in order; `what_next` passes its
kind's for a tie), `GuessSongShape` that or the track name's, and `LayOutShape` the
layout: an empty track gets frames of its own length where every part is
whole frames of it, else of the parts' greatest common divisor in bars; a
track with notes keeps its frames (`Refitted`, `Cut`, `Missing`). The
`song_shape` tool (`SongShapeTool` after `NameSectionTool` in `Tools.cpp`)
writes it as one group: `SetMeter` for a new frame length, then one
`WriteSong` (frames added or taken out at the end, the empty track's jumps,
skips and halts replaced by the loop's `Bxx` or a jingle's `C00`, a song
with notes' end jump moved to its new end) with `SetSections`; the frames
put in play patterns in frame order (`FreePatternForFrame`, see Known
pre-existing bugs, `TestFramePatternsInOrder`). `what_next`'s
`Frame` gives it after the key on an empty track without sections
(milestone and skip word `shape`, `ShapeForKind`); `Parts` writes the empty
song's A chords on the laid-out A (`FitProgression` fits a progression to
a part's bars), the form is done once a matched shape's parts all have
notes, and `GrowForm` fills a laid-out part after the body where it is.
It's in `CoreTools`. FamiTracker's Module > Song shape (`ID_MODULE_SONGSHAPE`,
also in `IDR_FRAME_POPUP`, routed by `CFrameEditor::OnModuleSongShape`, and
the piano roll ruler's right-click) is `Source/SongShapeDlg.cpp`, which
asks before adding bars to a song with notes and runs the tool through
`CMcpConnection::RunTool`. `SectionHoverLine` gives a section's line for
the frame editor's tooltip (`CFrameEditor::OnToolHitTest`, MFC's
`EnableToolTips`) and the piano roll's status line (`HoverSection`).
`SectionColor` gives "A'" and "A''" A's color. FakeSong's `SetMeter` is
undoable now, as `CMcpMeterAction` is. `TestSongShapes`,
`TestSongShapeTool` and `TestWhatNextShape` cover them, and
`TestWhatNextLoop` follows the shape-first loop.

What next panel and hearing the problems (from `feat-guide-panels`): View >
What next (`ID_VIEW_WHATNEXT`, `Source/WhatNextPanel.cpp`, a bar docked
along the bottom or the right as the problems panel is, after it in
`ArrangeBars`) runs `what_next` with `format` `json` through
`CMcpConnection::RunTool` a moment after `SongChanged` (not while the song
or its own `CBarsPlayer` plays, a dialog is open, or the user typed within a
second or twice the last ask's time; it polls `SongSettingsSignature`,
shared with the problems panel, plus saving and the path) and shows the step
in plain words with the recipe's stages as pills. The JSON is
`WhatNext::JsonReply` (`Work` runs the stages for both replies; `StepWhat`
says a step in plain words, naming bars that are a part by the part
(`PartOf`) and an empty song's shape (`song_shape`, a step of the frame) by
its title, `StepPlace` where it works; `Step(NextStep)` keeps a fix's
`Calls` and `Severity`, and `MelodyPlace` the melody's bars and `Own`, a
step offering notes to start from); each step id's and milestone's stage and
the panel's reader are `Source/Api/NextSteps.cpp` (portable; `GrownVersions`
reads `grow_melody`'s versions). Do it, Grow one (the version picked plays)
and Play the fix make their call between `BeginActionGroup` and
`EndActionGroup`, so it's one Ctrl+Z, labelled for Claude's undo, and a call
that fails is undone (the action serial says whether anything can be undone;
instruments added stay, as everywhere). A step's buttons wait while its
answer is stale (the song changed since); skips and the kind chosen are kept
per song and track while FamiTracker runs. The problems panel's lines get
Play it and Play the fix (`line_button_t`; on a narrow panel the selected
line takes a third line for them, `Heights`): `ProblemList::PlayOf` gives
the bars (a bar before and after the line's note or finding, at most
`PROBLEM_PLAY_BARS` of a long finding's own) and the channels to hear alone;
`Source/BarsPlayer.cpp` plays song rows through
`CSoundGen::SetNextPlayRange` with the other channels muted, then in the
mix, telling the range's end (`PlayRangeEnded`) from a stop. Play the fix
asks Keep or Take back in `CPlayFixDlg` (`IDD_PLAYFIX`), modal, so neither
the user nor Claude Code (`CanChange`) changes the song meanwhile, and Take
back undoes only the fix's own action. `TestWhatNextJson` (every state of
`TestWhatNextLoop`'s walks, the JSON against the text, and each kind's
shape: its title, one undo, then A's chords on the A it laid out) and
`TestProblemPlay` cover them.

Listening (from `feat-listen`): `listen` (`ListenTool` before `// Checks` in
`Tools.cpp`, after `MeasureMixTool`) renders the selected track's mix and
each part alone in one pass (`SongHost::RenderParts`), turns each part's
notes into where they should sound (`HeardNotesOf`: the row marks, `Gxx`
and `Sxx`, the sticky `Pxx` and `4xy`, the FDS's modulator from the
instrument and `Hxx`-`Jxx`, slides and arpeggios left out) and hands that to
`ListenToSong` in `Source/Api/Listen.cpp` (portable, its own `listen_tests`):
each held note's pitch by YIN (`transcribe::TrackPitch`) with its wobble,
rate and where it holds (`TrackNote`), clicks cycle by cycle
(`InsideClicks`, on the 2A03's and MMC5's pulses, `ListenPart::Restarts`),
pops, drones, notes hardly heard, beating pairs and, in the mix, clipping,
loudness jumps (`SpansLoudness`), harsh and muddy bars and the melody
covered, each with its measured thresholds. A low finding (a detail that may
be meant, as 42's slow flute vibrato) carries no call: `ListenFixOf` makes the
call `ListenCallOf` gives advice to hear it with the user first (and the short
reply says so), while high and medium keep theirs (`TestListenLowAsAdvice`).
`ListenNoteTuning` gives a note's
pitch by 0.4.6's tables (the chips' octaves, an N163 wave's cycle, `Pxx`
steps), and `ListenVibratoCents` and `ListenVibratoDepth` what `4xy` swings,
which `add_vibrato` uses to pick its depth by cents on every chip
(`PulseDepthOn`); its `modulator` `off` puts `H00` on each FDS note and the
vibrato where the modulator came in. `Source/Nsf/FdsModulator.cpp`
(`FdsModulatorMotion`, `FdsModulatorWords`) works out what a modulator does
as `APU/FDSSound.cpp` bends it, for the studies (`DescribeStudy`), the
references (`ReferenceText`, `ReferenceFdsSound`), `design_instrument` and
`check_song`'s warble (`FOUND_WARBLE`, `PROBLEM_WARBLE`; the problems panel
keeps its folded kinds by name, "Folded kinds"). FamiTracker reports an FDS
instrument's wave and modulator (`InstrumentInfo::FdsWave`, `ModTable`,
`ModSpeed`, `ModDepth`, `ModDelay`); FakeSong renders notes as their chips
sound them when `ChipPitches` is set. `TestListenTool` and
`TestListenFdsWarble` cover the tool. what_next's listen is below (Small
fixes, second batch), finish_song's and the problems panel's Listen in The
listen's rest.

Exercises (from `feat-exercises`): Help > Exercises (`ID_HELP_EXERCISES`,
`Source/ExercisesDlg.cpp`) lists the ten of `Source/Api/Exercises.cpp`
(portable, ASCII: `Exercises()` in order, each with its checks;
`ExerciseChecks()` with what each teaches) and keeps each one's state in the
"Exercises" registry section (0 not tried, 1 started, 2 done). Starting one
(`StartExercise`, also `ID_HELP_EXERCISE_FIRST` + its place, which the
note's Next exercise posts) opens its starter (`res/exercises/<id>.ftm`,
RCDATA, through `OpenBuiltInSong` as the example songs open) or, for the
song's shape and the first song, a new song (`ID_FILE_NEW`), then its
script (`res/exercises/<id>.txt`) in the tutorial note (`StartExerciseNote`).
Scripts are tutorials with check steps: `check:` (`WAIT_CHECK`, no `wait`),
`hint:` and `answer: <channel>, bar N[, instrument]: <steps>` lines; their
texts mark glossary links as `[[words]]` or `[[words|id]]` (`UnmarkLinks`),
which exercise notes draw with `LayOutLinked` in `Tutorial.cpp`. Check runs
the `check_exercise` tool (`CheckExerciseTool` after `WhatNextTool` in
`Tools.cpp`, format json: `ExerciseVerdictJson`, read back by
`ReadExerciseVerdict`); `ExerciseFactsOf` reads the song as the tools do
(the target channel: the 2A03's Pulse 1, the triangle or the noise; the key;
the chords the other parts play, `HearChords` on `HeardNotes` without the
target; `BreathsOf`'s spans; the drums' kinds, `ExerciseDrumKind`; the
sections with their `PartLine`; the flow; the melody as `what_next` guesses
it) and `CheckExercise` judges it. A check step that passes stays (no
auto-advance), and the exercise is done when its last check passes. Hear it
(`HearGoodAnswer`) writes the answer lines with `write_notes` as one action
group, plays its bars in `CExerciseHearDlg` (`CBarsPlayer`) and undoes them
(`ForgetRedo`); Play it plays the verdict's bars, its channels alone first.
The starters (2A03, one frame of four bars, two for the beat, the first
example song's instruments, a `BF KEYS` block choosing C major except in
minor's) are written from step parts by `tools/exercises/make_starters.py`
(Python alone, from `res/examples/first-song.ftm`; its README says how to run
it and the others, which write `res/exercises` byte for byte); keep each check failing
on its starter and passing on its script's answers, clean as `check_song`
hears them. Each part keeps lanes of its own in the piano roll (the tune
from C-4 up, the chords under it, the triangle's bass from E-2 to D-3), so a
note drawn there never hides another channel's: no answer's note may start
in a beat where another channel holds its pitch (`LanesTaken`). A
check step puts the cursor on its answer's channel, and the note scrolls the
piano roll to the answer's lanes (`ShowAnswerLanes`,
`CPianoRollWnd::ShowPitches`) as each step begins and when the piano roll
changes its size; once a step's bars or Play it have played, the cursor goes
back to their first row (the drum grid follows it). A note drawn, moved or
pasted up to the song's last row no longer adds a frame: the piano roll
leaves an end alone past the song's end unwritten (`CChannelNotes::OnlyEnds`
in `Recompute` and `Commit`), which made the answers' songs 8 bars of which
4 droned. To add an exercise: its checks in `Exercises.cpp`, its script
and starter in `res/exercises` (each part in its lanes), the
RCDATA lines and IDs, a row of `FILES` in `ExercisesDlg.cpp`, and its name
in `TestExerciseScripts` (`tutorial_tests`). `exercises_tests` runs the
checks on songs described by hand; `TestExerciseStarters` (every script and
starter, read as FamiTracker opens it by `LoadStarter`; each check failing
as it opens and passing on its answers, the first song by following
`what_next`), `TestExerciseMistakes` and `TestExerciseTool` in `ApiTests.cpp`
and `TestExerciseLines` in `tutorial_tests` cover the rest.

Newcomer fixes (from `feat-newcomer-fixes`): a click on another channel's
note in the piano roll (a faint one) does what a click on an empty spot does
for the bright channel (Draw puts a note in, Paint paints, Select starts a
box), so a click near one's own notes never switches parts; a double-click
there edits that channel (`DoubleClick`), taking back what its first click
did while that's still the last change (`Impl::FaintClick`, noted in
`MouseDown` and `MouseUp`; `TakeBack` undoes it and forgets the redo). The
status line over a faint note says so first (`FaintNoteHint` in
`PianoRollUi.cpp`), as do `RollHint`, the clips tutorial, two exercises and
the glossary's piano roll. The panels along the bottom share a row and keep
the note views room (see the views above). A tutorial's note moves the
glossary off the part a step shows and off the note, once as each step shows
its part and as the glossary opens (`KeepGlossaryClear` in `Tutorial.cpp`, to
where it covers the least of them), then places itself clear of it too
(`Place`). The first small song's new song is named for the exercise
(`StartExercise`), and `what_next` lays out the shape a track's name asks for
where it's one of the kind's that loops (`ShapeForKind` with the track's name
in `SongShapes.cpp`): "A first small song" or a "Shop" gets the short loop of
14 bars rather than the general recipe's town of 40. A town's words (town,
village, city, square, market, harbor, port) ask for the town and come before
a small place's (`PRIORITY`, where the small shape is last: a "Home village"
is a town; `TestShapeTownWords`). `check_exercise`'s list
drops each line's full stop. `TestPanelBands` (`noteviews_tests`),
`TestFaintHints` (`pianoroll_ui_tests`), `TestExerciseListPeriods`,
`TestShapeByTrackName` and `TestFirstSongSmall` (the first song through What
next to its end) cover them.

Asking your own model (from `feat-ask-panel`): View > Ask (`ID_VIEW_ASK`,
`Source/AskPanel.cpp`, a bar in `PlaceBottomPanel`'s row or on the right,
after What next in `ArrangeBars`) asks the user's own model, any
OpenAI-compatible chat server (llama-server, llama-swap, Ollama, LM
Studio). The portable part is `Source/Api/Assistant.cpp` (the settings and
the address, `AssistantFunctions` from `ToolSet::List()`'s schemas, the
system prompt: a newcomer's guide, then `ToolSet::Instructions` or
`CoreInstructions`; `AssistantContext`; the streamed reply:
`HttpStreamReader`, `SseReader`, `AssistantStream`; and `AssistantChat`,
the conversation and its rounds: reads run at once, a change is
`CALL_PROPOSED` in Ask first or runs in Let it work, and `Fits` shortens
older tool replies, then leaves older questions out, to stay in the
context) and `Source/Api/AssistantSocket.cpp` (an exchange per request,
Stop heard every 200 ms, bounded waits, and `AssistantTakeReply`, shared by
the panel and the tests: a refusal of tools, llama-server without
`--jinja`, turns the conversation's tools off). `AssistantToolKinds` lists
every tool as reading, changing or depending on its arguments;
`TestAssistantKinds` keeps it in step with `ToolSet::List()` and the
read-only hints, so a new tool goes there too. The panel runs each request
on a thread of its own (`ask_job_t`, its events posted with a serial) and
the tools on the UI thread between the replies through
`CMcpConnection::RunTool` (`Core` for the small set's caller; `ToolList`
gives the tools), a change between `BeginActionGroup` and `EndActionGroup`
labelled "(the Ask panel)" (Take back all undoes the run's while they're on
top of the history), and keeps a conversation per song (`talk_t`, by path,
or serial while untitled). Its settings are the "Ask" registry section (the
key through DPAPI, `CryptProtectData`), the bar's the "Ask panel" one;
`CAskSettingsDlg` (`IDD_ASK_SETTINGS`) runs Test connection on a thread.
The exercise in progress comes from `ExerciseInProgress` in `Tutorial.cpp`;
without a model, `AssistantSearchHelp` searches the glossary, the exercises
and the built-in tutorials. `assistant_tests` (`tests/AssistantTests.cpp`,
with `tests/FakeModel.h`, a scripted model over a real socket) and
`TestAssistantKinds`, `TestAssistantFunctions`, `TestAssistantContext` and
`TestAssistantLoop` in `ApiTests.cpp` cover it.

First steps (from `feat-first-steps`): Help > Exercises shows the exercises
in groups (`ExerciseGroups()` in `Exercises.h`: First steps, Next steps and
Making it your own, a heading with `Later` set and no exercises yet;
`ExerciseInfo::Group`), its list owner-drawn (`CExercisesDlg::OnDrawItem`:
bold headings with how many are done, item data `GROUP_ROW` + group for a
heading, else the exercise's place); the first not done is picked, so a
newcomer gets First steps' first. Its eleven exercises (`first-note`,
`high-low`, `long-short`, `steady-beat`, `loud-soft`, `stairs`, `rhythm`,
`copy-repeat`, `new-sound`, `two-parts`, `tiny-tune`, each one check of
its id, after `CheckFirstSong` in `Exercises.cpp`) look at Pulse 1 (the
triangle under it for `two-parts`), pass any reasonable answer, word
their next step in the piano roll's terms (lanes, beat lines, a note's
right edge, Select, Instrument > Give the selected notes, Effects >
Volume) and say what they taught in one sentence; `tiny-tune` leads into
`home-note`. Their starters (1-4 bars, one part at most, C major chosen,
written from step parts by `tools/exercises/make_first_steps.py` as Next
steps' are, their scripts by `first_steps_scripts.py`) and scripts are
RCDATA named
`FIRSTSTEPS_*_SONG` and `_SCRIPT` in `FamiTracker.rc`, without numbers in
`resource.h` (`FILES` in `ExercisesDlg.cpp` holds the names;
`ReadBuiltInText` and `OpenBuiltInSong` take a resource's name or
`MAKEINTRESOURCE`), and `ID_HELP_EXERCISE_FIRST`/`LAST` are 38050-38099.
The piano roll's Effects menu has Volume (`GROUP_VOLUME`, `FX_VOLUME`:
`CChannelNotes::SetVolume`, which writes the notes after them their old
volume as `SetInstrument` does instruments), a quieter note is drawn
thinner from the top and says its volume when pointed at (`HasVolume`:
not the triangle or the samples), and an empty song's groove box stays
out while an exercise's note is open (`ExerciseRunning` in `Tutorial.cpp`).
The glossary has `length`, `part` (both with Hear it) and `lane`.
`TestFirstStepsList` and the `Test...Checks` of `exercises_tests`,
`TestFirstStepsMistakes` beside `TestExerciseStarters` in `ApiTests.cpp`,
`TestFirstStepsTerms` (`glossary_tests`) and `TestVolume`
(`noteedit_tests`) cover them.

Exporting from another computer (from `feat-remote-export`): a client of the
connection can't use File > Save As, so `save_song` with `name` (or without,
for a client: `Caller::Client`, which `HandleMcp` sets on every request) saves
a song without a file into the Songs folder next to FamiTracker.exe
(`SongsFolder` in `Source/AppFolders.cpp`), in a folder of its own, numbered
when taken (`SongHost::SaveNew`, `NumberedSongName`; `CMcpLink::SaveNew` makes
the folder with `CreateDirectory`, then `OnSaveDocument` and `SetPathName`),
and a client's `export_for_game` and `finish_song` do that first
(`SaveIntoSongs`, `NewSongName` in `Tools.cpp`), and `what_next`'s save step
gives a client `save_song` with that name (`WhatNext`'s `m_Client`,
`TestWhatNextSavesForClient`). FamiTracker's own windows
call the tools with a plain `Caller`, which still asks for File > Save As.
Over the network the reply's links carry a code of their own in the address
(`/files/<name>?code=`, `FilesCode` in `Mcp.cpp`: HMAC-SHA-256 of the access
code, as `PhoneCode` is; the access code itself is refused there), with the
Tailscale address too for a phone away from home (`Caller::AwayUrl`), and
`FileRoute` serves the file inline with one byte range (`ParseByteRange` in
`Network.cpp`: 206, 416, `Accept-Ranges`) and HEAD. The OGG files next to the
song are MCP resources (`famitracker://files/<name>`, `ExportUri`;
`resources/list` through `SongHost::ListExports`, `resources/templates/list`,
`resources/read` as a blob up to 32 MB); a reply links its files
(`ToolResult::Files`, `resource_link` only from 2025-06-18: `Endpoint::Version`,
from `MCP-Protocol-Version` or `_meta`) and `attach` carries them (embedded
resources, at most `MAX_ATTACHED` in all, `ReplyFiles`). `famitracker-mcp`
sends the version agreed in `initialize` with the requests after it
(`BridgeAgreedVersion`), as an HTTP client does. `TestNetwork` (ranges, the
code in the address), `TestSaveIntoSongs`, `TestExportResources` and
`TestBridgeResources` (over real sockets) cover them.

Small fixes, second batch (from `feat-small-fixes-2`): `check_song`'s key
check leaves out a borrowed chord's and a secondary dominant's notes, as
`write_notes`' warnings do: `ChordedOutsideKey` (after `PitchOutsideKey` in
`Tools.cpp`, which both ask) takes a note outside the key that makes a major
or minor triad with two notes the other parts sound with it (their
arpeggios and its own counted) that `RoleOfChord` accounts for in the key
(borrowed, leading to another chord, the Neapolitan, a mode's tension; not a
surprise), and the class's other notes in that bar with it. check_song
does so only while under `KEY_COLOR_SHARE` of the notes fall outside the key,
so a key chosen wrong keeps every finding (`TestKeyCheckChords`). Written back
section by section, the soundtrack's 57 finished tracks and cues ask one key
question with this rule, the same one as with write_notes' old rule (any major
or minor triad with the other parts' notes left out).
`what_next`'s count of steps done never falls along a walk (every
`FollowWhatNext` checks it, `StepsDoneOf`): its melody milestone is the first
part's (a later part's melody, after its chords, is the form's, which isn't
done without it), `Depth` leaves the second line and the repeats varied
undone while the form grows (`Growing`), and a repeat's line of its own is a
part playing its bars otherwise than in the original, not `PartsBeside`'s
voices (a draft's triangle doubling the bass was one in A and A2 alike,
until check_song's fix took it out). The kind chosen in What next
(`ChosenKinds` in `WhatNextPanel.cpp`, read by `WhatNextChosenKind`) goes
into the Ask panel's context (`AssistantFacts::Kind`): its what_next step is
asked in that kind, and `AssistantContext` tells the model to give
what_next, get_recipe and finish_song that `kind` (`TestAssistantContext`).
`what_next` asked for one `listen` before `finish_song` once the parts are
written, which is now the finish's own (The listen's rest, below;
`WhatNext::Listened`, milestone `listened`, skip word `listen`, stage
polish in `NextSteps.cpp`): `ToolSet::Call` keeps a
listen over the whole track with the host (`SongHost::KeepListen` and
`LastListen`, its JSON with `SoundPrint`, while the host runs), the step is
done while the print matches, and its high findings with a call are steps
as check_song's are (`FindingFixStep`, shared with `Breakers`); with nothing
left to finish or export no listen is asked. `listen` is in `CoreTools` (short there)
and `CoreInstructions`' Finish line (`TestWhatNextListens`, and the walks).

The listen's rest (from `feat-listen-rest`): the What next panel's Do it of a
slow call (`listen`, `finish_song`, `export_for_game`: `WorkingWords` in
`WhatNextPanel.cpp`) says what runs in the panel and the status bar before
the UI thread waits for it (`ShowWorking` draws both at once), and the panel
keeps the wait cursor while its call runs; a render (`RenderFiles` in
`McpConnection.cpp`) puts the status bar's text back when it's done, where
it cleared it, so the line stays through the listening after the render.
`finish_song` listens after its polish, mix and seam (`skip` `listen` leaves
it out; a cue or a sound effect isn't listened to): `listen` with `format`
`json` on the melody it follows, kept with `SongHost::KeepListen` as
`ToolSet::Call` keeps one (a kept listen whose print is the song's is used
again, without a render), and `FinishListenLines` (before
`FinishSongTool`) writes its lines: a count, each high finding with a fix as
a `Heard:` line with its call (`FixText`), and the `export_for_game` call to
make the file again once they're fixed; the export still follows.
`what_next`'s finish step names the listen and passes `skip` `listen` when
it's skipped, and with nothing left to finish, a kept listen of the song as
it is still gives its high findings' fixes (`WhatNext::HeardFixes`, shared
with `Listened`), so a fix the finish heard comes before the file is taken
as done (`TestFinishSongListens`). Since finish_song listens, `FinishStep`
asks no listen before it (a walk listens once): a kept listen of the song as
it is (`KeptHeard`) counts and gives its fixes, else `listened` is left for
the finish; a listen step comes only before an export alone (`Finish`, while
the export is due and the song changed since it was heard, as after a fix),
and `ListenWanted` says whether one is asked (`TestWhatNextListens`). The problems panel's Listen (the
header's button, F6 and its menu) runs `listen` with `format` `json` through
`CMcpConnection::RunTool` (so it's kept for what_next too) with the wait
cursor and `LISTENING` in the header and the status bar, and keeps the reply
(`Heard`, of a song by `GetSongSerial` and a track), which every check adds
to check_song's (`ProblemList::Add`; `Read` is `Clear`, then `Add`). Its
findings are kinds of their own by severity (`ProblemHeard`):
`PROBLEM_HEARD` (high, after the warbles), `PROBLEM_HEARD_LOOK` (medium,
after out of range) and `PROBLEM_HEARD_DETAIL` (low, the details to hear,
last), with Play it (two parts that beat play both) and Fix for a fix of
one call; after a change they stay listed as heard before it (`SongMoved`,
`IsStale`: Explain only) until the next listen (`TestProblemListHeard`).

Wine checks (from `feat-wine-checks`): the status bar's clock is
`CSoundGen::m_iClockTicks` (`GetPlayerTicks`), which only a song that plays
counts; `m_iPlayTicks`, which 0.4.6 showed, counts a render too (it ends the
render, and File > Create WAV's dialog shows it), so after every listen,
measure_mix or export the clock showed the render's length. A tool that
renders still stops a song that plays, as File > Create WAV does. The Ask
panel gives the keys back to its question box after its Settings dialog
(`ShowSettings`), where MFC had given them to the pattern editor and a
question typed next played notes, started editing and played the song.

New songs and opening another (from `feat-new-song`): `new_song`
(`NewSongTool`, after `SaveTool` in `Tools.cpp`) puts a new song in place of
the open one through `SongHost::NewSong` (`CMcpLink::NewSong` sends
`ID_FILE_NEW` as Help > Exercises does, so `OnNewDocument` applies the
profile's template, then `SwitchChips` with `NewSongSettings`' chips, which
`NewSongChips` reads from `expansion` as `set_expansion` reads its chips;
the template's VRC6 sounds go without the VRC6 and a VRC6 Plus gets
`AddSoftLayerSounds`; then the title, `TitleOfName`'s whole words within 31
characters, the track's name and `MakeUntitled`: no file, nothing to save,
no undo history). A client's (`Caller::Client`) is saved at once with
`SaveIntoSongs`. `open_song` (`OpenSongTool`) lists `SongHost::ListSongs`
(`CMcpLink::ListSongs`: the `.ftm` files in the Songs folder and its
folders, `CFamiTrackerApp::GetRecentFiles` and the open song, each once as
a `SongPlace`) the newest first (`SongListText`, `MAX_LISTED_SONGS`), finds
one by name (`FindSongToOpen`) and opens it with `SongHost::OpenSong`
(`CMcpLink::OpenSong` reads it with `nsf::ReadFtmFile` first, so a file
FamiTracker can't open is refused with the reason instead of a message box
waiting for nobody, then `theApp.OpenDocumentFile`). It takes a `path` only
from this computer (`Caller::Remote`, set in `HandleMcp`;
`IsSimpleSongPath`: a drive, plain folder names and a `.ftm` file). Both
refuse while the open song has changes not saved (`RefuseUnsavedSong`, and
the hosts again) and stand with `import_nsf` in `AfterCall`'s `ASIDE`.
FakeSong keeps what it saves (`Disk`, `RecentFiles` and `OpenPath`, kept by
`SaveNew` and `Save` through `KeepOnDisk`) and replaces its song as
`CMcpLink` does; `TestNewSongs` covers both tools, through `HandleMcp` from
the network too.

Clashes and their fix (from `feat-clash-fix`): `check_song`'s clash detection
is `HearGrid` and `ClashRuns` (before `PolishTool` in `Tools.cpp`, beside the
touch that fixes them), and `ClashKind` tells a clash worth fixing at once
(held a beat or more, `OnStrongBeat`, chord parts that disagree) from a
detail that may be meant (`CheckFinding::Severity` low, `detail` `passing`
for a passing note shorter than a beat, `IsPassingNote`, or `color` for a
tone of an arpeggio's chord: 239 of the finished soundtrack's 243). `polish`'s
touch `clashes` is `FixClashes`: the note no other part sounds over the half
bar goes first (`Support`), the other only where that's the melody's and
it's a chord voice (`ClashRole`: never the melody, a line or a note that
slides; the bass only where its own note disagrees, never its root), to the
nearest note of the chord the others sound that rubs against nothing
(`RubsAgainstOthers`; a held note keeps out of another's unison when it can,
`Doubles`); a held third stays for a detail or where no other part plays it,
and `KeepsChords` compares the chords the parts play row by row (names for a
detail; root, third and no tone the move adds for the rest). It goes over the
song until nothing moves. Each clash it clears carries that call over its
bars and two channels as its `fix` (planned per finding, `Plans`), `what_next`'s
`Breakers` gives one `clash` step for the song when a clash worth fixing has
one, `WritingWarnings` warns of the clashes it would clear with the call
(written back section by section, the finished soundtrack gets none), and
`ProblemList` reads a low
clash as `PROBLEM_CLASH_DETAIL` (appended, `clash_detail`). Measured: no note
of the finished soundtrack moves and `what_next` asks no new step on its 79
tracks; a local model's calm song went from 46 clashes to 14 (2 worth fixing,
a held third under the tune, and 12 passing notes over the pads' thirds) in
one call. `TestClashFix` covers it.

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
  - After a green build of `beginner-friendly`, `next`, `recorder` or `api`,
    the `publish` job replaces that branch's rolling GitHub release (tag
    `build-<branch>`) using `.github/scripts/publish-build.sh`.
    `beginner-friendly` is marked Latest; the others are pre-releases. Each
    release has a zip with `FamiTracker.exe`, plus `wav2ftm.exe`,
    `famitracker-mcp.exe` and `nsf2ftm.exe` when the branch builds them
    (`api` has no `famitracker-mcp.exe`), `famitracker-mcp-linux` (the
    bridge for Linux, built in the Linux job in an Alpine container), and a
    debug-symbols zip. These
    tags are build pointers and get deleted and recreated; never use them
    for real releases.
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

The application has no test suite of its own. What exists:

- **CI build:** every push compiles Win32 Release and Debug (see Building).
- **Transcription engine** (`Source/Transcribe/`): plain C++14 that builds and
  tests on Linux with `cmake -S Source/Transcribe -B build/transcribe`,
  `cmake --build build/transcribe` and
  `ctest --test-dir build/transcribe --output-on-failure`. CI runs these tests on
  Linux and with MSVC, and lists the result in the Release build report.
- **Clips, note editing, chip switching, the drum grid, tutorials, the
  views and the sound chips** (`Source/Clips.cpp`, `Source/NoteEdit.cpp`,
  `Source/ChipSwitch.cpp`, `Source/DrumGrid.cpp`, `Source/TutorialScript.cpp`,
  `Source/NoteViews.cpp`, `Source/APU`): `Source/Tests` builds
  them on Linux against working stand-ins for the MFC and document pieces
  they use (`Source/Tests/standins/`). `ClipsTests.cpp` covers linked copies
  following edits, unlinking, undo snapshots, tracks, and the `BF CLIPS`
  block read back, cut short at every byte. `NoteEditTests.cpp` covers the
  piano roll's edits: notes making way, releases and halts, volumes and
  instruments kept, switch-off effects, song-wide effects staying, copy and
  paste, and the effects the piano roll shows on notes and puts on them
  (lasting ones kept for the notes after). `ChipSwitchTests.cpp` covers the
  channels a set of chips has, the notes counted on the channels a chip
  switch clears, the channels emptied, their song-wide effects moved to the
  last channel with patterns of their own where frames share one, and the
  channels that stay moved to their new columns. `DrumGridTests.cpp` covers reading the
  drums off a bar, putting them on and off, the ready-made beats, the bar
  the panel follows and its buttons in lines. `NoteViewsTests.cpp` covers
  View > Pattern editor and View > Piano roll from every layout: there's
  always one view to place notes in.
  `TutorialScriptTests.cpp` reads tutorials, every wait and command name and
  the errors, and the built-in tutorials, which must stay ASCII.
  `ApuTests.cpp` plays every chip's channels by register writes (each
  rendering forked, since the emulators keep state in globals): all in the
  middle, both sides must be the mono mix of the code before stereo,
  also with Soft and Space at 0 or set and cleared before a sound (the
  depth tests also check a channel through its own buffers within 2 steps
  of the mix on every chip, Soft's response, each room's tail, the echo's
  repeats and determinism; `TestDacStart` a DPCM level started without a
  step, `TestSilentParts` a 2A03 channel left out of a render: the mix less
  it alone), pinned by hashes (`MONO`; after a deliberate change to the emulation,
  configure with `-DAPU_REFERENCE=<a folder with that code's APU and
  Blip_Buffer>` and `apu_tests` prints them anew); two channels panned
  apart must each be alone on their side as they sound in the middle. The
  APU sources are copied under the names they're included by, next to
  stand-ins for `stdafx.h` and `Common.h` (`standins/apu`). Same three
  commands with `Source/Tests` and `build/tests`; CI runs them in the Linux
  job. The stand-ins must keep the real headers' signatures.
- **Claude Code connection** (`Source/Api`): JSON, HTTP
  framing, the MCP protocol and every tool against a song in memory, and
  the WSL bridge, also over real sockets (`tests/ApiTests.cpp`);
  `TestNetwork` covers other computers: the address filter, Host headers,
  the core set, `send_file`, the file routes with traversal attempts and
  `export_for_game`'s links, and `TestCheckJson` and `TestProblemList`
  read `check_song`'s JSON back as the problems panel reads it;
  `TestSections` and `TestSectionTools` cover sections as frames move, the
  `BF SECTIONS` block (read back, cut short at every byte and damaged) and
  the tools that name them or take them in place of bars; `TestKeyChoices`
  and `TestKeyTools` the keys' shading, snapping and steps, the `BF KEYS`
  block (the same way), `set_key`, the tools' default key, keys moving with
  the whole song, versions, and the piano roll's chords (`ToolSet::Harmony`)
  for known bars; `TestMixerStore` and `TestMixerTools` the `BF MIXER`
  block (the same way), `set_mixer`, `get_song`'s list, `mix` counting a
  fader, the fader and the pan in the rendered files, and Write into the
  notes in two tracks (the same loudness after, one undo for all);
  `TestMixerDepthStore`, `TestMixerDepthTools` and `TestCutTail` the
  depth: the block's version 2 (the same way), `set_mixer`'s `soft`, `space`,
  `room`, `echo` and `repeats`, `mix` unchanged by them, and the export's
  loop from the second time through and a halt's tail;
  `TestVrc6Plus` the `BF VRC6PLUS` block (read back, cut short at every
  byte, damaged, another version), the starter sounds and their wave places,
  `set_expansion` to and from the VRC6 Plus, its names old and new, the
  soft layer in the sound index, voiced chords and `mix` on it, and the
  N163's level by channel count;
  `TestMeasureMix` `measure_mix` against the parts rendered alone (nothing
  changed, the renders' silent channels, sections, the intro, the sides,
  the targets, `loudness`, named parts);
  `TestSoundIndex` and `TestFindSound` the sound browser's
  index on every chip (homes, kinds, words, the two warnings, previews, the
  song's own instruments) and `find_sound`, `ToolSet::Sounds` and
  `MakeSound` against `reference_sound`. Same three
  commands with `Source/Api` and `build/api`; CI runs it on Linux and with
  MSVC. The song in memory (`FakeSong`) writes changes the way
  `CInsertRecordingAction::Redo` does; keep the two in step, since a
  difference between them hides bugs from the tests.
- **NSF importer** (`Source/Nsf`): the 6502, files and playlists, loops,
  rows, notes, instruments and studies, on NSFs the tests build with their
  own 6502 program (`tests/TestNsf.h`); and tracker modules
  (`tests/TrackerTests.cpp`, on MOD, S3M, XM, IT and MIDI files built byte
  by byte, cut short and corrupted). Same three commands with
  `Source/Nsf` and `build/nsf`; CI runs it on Linux and with MSVC. For
  fidelity, convert real NSFs and compare FamiTracker's playback of the
  result with the NSF (`Simulate.h`).
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
  compiling it with `g++`. The release zip of a branch (`build-<branch>`,
  downloaded with `gh api` from the releases' REST endpoints) also runs under
  Wine: `wine32` on Xvfb, with a PulseAudio null sink so DirectSound starts
  and a Wine virtual desktop so windows get focus. `xdotool` clicks and types
  and ImageMagick's `import` takes screenshots, enough to try dialogs and
  drags; drags need the mouse moved in steps.

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
- **`BF SECTIONS` block** (this branch; version 1, written after `BF CLIPS`
  only when a track has sections): an `int32` count of tracks, then for
  each track its `int32` number, an `int32` count of sections and, for each
  section, `int32` first and last frame and its name NUL-terminated (1-32
  bytes, in the code page). Tracks come in order; sections are in order,
  don't overlap, lie within the track's frames and have names unique
  regardless of case. `SectionStore::Read` checks every read, and a damaged
  block or another version is left out: the song loads without sections.
  Anything that changes a track's frame list must keep them in step
  (`SectionsInsertFrames`, `SectionsRemoveFrames`, `SectionsMoveFrames`,
  `SectionsCopyFrame`, `SectionStore::FitFrames`).
- **`BF KEYS` block** (this branch; version 1, written after `BF SECTIONS`
  only when a track has a chosen key): an `int32` count of tracks, then for
  each, in order, `int32` track, tonic (0-11), mode (its place in
  `Api/Music.cpp`'s `MODES`, so those stay append only) and flags (1:
  pentatonic, for major and minor only). `KeyStore::Read` checks every read
  and refuses a block longer than its tracks; a damaged block loads as no
  chosen keys. Track removal, swaps and imports keep it in step
  (`TrackRemoved`, `TracksSwapped`, `FitTracks` after loading).
- **`BF MIXER` block** (this branch; version 1, written after `BF KEYS`
  only when a channel of the module is off 0 dB or the middle): an `int32`
  count, then for each channel, in increasing order, `int32` `chan_id_t`,
  level in tenths of a dB (-240 to 60) and starting pan (0-255).
  `MixStore::Read` checks every read and refuses a block longer than its
  channels; a damaged block or another version loads as every channel at
  0 dB in the middle. `chan_id_t` is append only, so the IDs stay valid.
  Version 2 (`MixStore::WriteVersion`, only when a channel's Soft or Space
  or the space is off its default, so songs with faders alone still save
  as version 1): each channel also has `int32` Soft and Space (0-100), and
  after the channels come `int32` room (0-4, `SPACE_ROOM_NAMES`, append
  only, as `CSpace` numbers them), echo (0-96 48ths of a beat) and repeats
  (1-8).
- **`BF VRC6PLUS` block** (this branch; version 1, written after `BF MIXER`
  only while the module is VRC6 Plus): one `int32`, the soft layer's
  channels (4). `Vrc6PlusMark::Read` takes exactly those 4 bytes of version
  1 with that value; anything else is ignored and the song opens as VRC6 +
  N163 (4 channels), as builds without it open it. `Source/Nsf/FtmFile.cpp`
  skips it quietly.
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
  index. On load, the index-to-channel mapping is rebuilt from the chips in
  `chan_id_t` order (`CSoundGen::RegisterChannels`), so reordering `chan_id_t`
  scrambles existing modules. With several chips on, the columns are the
  2A03's, then each chip's (the N163's up to its count), as `ChipChannels`
  in `ChipSwitch.cpp` lists them; the N163 channels past the count are
  registered after them.
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
  reuse the letters H, I and J. `8` is this branch's pan (`EF_PAN`, 33):
  FamiTracker 0.4.6 loads a module with it (nothing checks effect numbers
  on load), shows "(invalid)" on a row with it in the first effect column
  (the other columns stay blank) and plays it in the middle. The Claude
  Code tools (`Source/Api/Music.h`), `Source/Nsf/FtmFile.cpp` and
  `Source/Transcribe/TextModule.cpp` keep their own copies of the letters.
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
   chip-specific ones in the channel class's `HandleCustomEffects`. The DPCM
   channel's handler never calls `CheckCommonEffects`; an effect that every
   channel plays alike, the DPCM's too, can go in the effect loop of
   `CChannelHandler::HandleNoteData`, as the pan (`8xx`) does.
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

- **Other computers (the connection over the network):** with the option
  off, FamiTracker binds 127.0.0.1 only and `HandleMcp` refuses any request
  whose `Peer` isn't loopback; keep both, so one mistake doesn't open it.
  Every route needs the access code. The file routes take plain file names
  only (`IsPlainFileName`: no folders, drive letters, streams or device
  names), the Inbox only the kinds the tools read (`IsInboxFile`) and
  `/files/` only `.ogg` (`IsExportFile`); never let them take paths. The one
  code besides the access code that opens a route is the export links' own
  (`FilesCode`, `/files/` only), and the access code is refused in an address. A tool
  added later either joins `ToolSet::CoreTools` or not: the tests check that
  `CoreInstructions` names only tools of the set and stays short, so keep
  its rules (notes that vanish, clicks, 0.4.6's effects) in step with
  `Instructions`. `famitracker-mcp` must keep building from `ftbridge`
  alone.
- **Pulse vibrato clicks:** `CSquare1Chan::RefreshChannel` (and the MMC5's)
  writes the period's high register (`$4003`) only when its value changes,
  and `CSquare::Write` resets the duty step on that write, as the 2A03
  does; the NSF driver behaves the same. A vibrato (pitch sequence or `4xy`)
  that takes a held note across a multiple of 256 in the period restarts
  the wave every time it crosses, about ten clicks a second. A-3 (period
  253) does with a gentle vibrato of 3 steps, F-1 (1280) with any; A-2
  (507), D-2 (761) and A-1 (1016) with deeper ones. A user heard these as
  "hiccups in long tones" in 19 of 30 tracks: keep `SmoothChanges` in
  `Commit` and the checks in step with the channel handlers.
- **The FDS in 0.4.6:**
  - It sounds two octaves under the note written: with `TRANSPOSE_FDS`,
    `CSoundGen::LoadMachineSettings` gives a wave of one cycle a quarter of
    the note's frequency (A-5 sounds 440 Hz, A-3 110 Hz), and the loader
    moved older files' FDS notes up two octaves. Its frequency register
    tops out at 4095, so G#7 is the highest note in tune (A-7 plays 13
    cents flat, A#7 and B-7 no higher), and FDS notes at written octave 2
    and under round to a coarse register (tens of cents).
  - Its emulator (`APU/FDSSound.cpp`) rounds the modulator's bend after the
    shift rather than on the product, so the bend goes 2 (bias under 0) or
    3 (bias 16 and over) 64ths of the pitch at the least, whatever the
    depth: about 55 cents down or 79 up. The editor's own "sine" table at
    depth 1 sits a quarter tone flat. `FdsSounds.cpp` makes gentle
    vibratos with the pitch sequence (on the FDS a positive value raises
    the pitch, and a unit is 1731 cents over the register) and modulator
    tables whose bends average out (`MeanBendCents` in the tests checks
    them).
  - The emulator writes a volume change at once, so a smooth wave starting
    at its peak at a random phase clicks; the sounds start over two frames
    or more. `CChannelHandlerFDS` doesn't reset the wave's phase on a note.
  - The mixer lowpasses it (`treble_eq(-48, 1000)` in `CMixer::UpdateSettings`).
- **`check_song`'s JSON is read by the problems panel** (`ProblemList.cpp`):
  keep its field names, and give a new kind of finding a name in both
  (`CHECK_KIND` in `Tools.cpp`, `KIND_IDS` in `ProblemList.cpp`, heard worst
  first); the panel leaves out kinds it doesn't know.
- **Bundled libraries as released:** `Source/libogg` and `Source/libvorbis`
  (BSD licensed) keep upstream's files and LF line endings; both builds turn
  their warnings off, and only `libogg/include/ogg/config_types.h` is ours.
- **Case-insensitive filesystem assumed.** 20 distinct `#include` paths, on 31
  lines, differ in case from the files they name (`"apu/apu.h"`, `"FDS.h"` for
  `APU/FDS.H`, `"fft\fft.h"`, ...), and several files have upper-case
  extensions (`APU/APU.CPP`, `APU/VRC6.H`). Search with `-i`, and don't fix
  case outside a deliberate portability change.
- **One folder for object files:** the project compiles every `.cpp` into
  one folder, so two files of the same name (in any case) in different
  folders overwrite each other's `.obj`: MSB8027, then unresolved
  externals. Name new files apart (`Nsf/MidiFile.cpp` beside `MIDI.cpp`).
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
- **Headers before `stdafx.h`:** it defines `_CRT_SECURE_NO_WARNINGS`, so a
  header that pulls in the C runtime (any of `Api/`'s) included before it
  brings back C4996 for `sscanf` and the like later in that file. Include
  such headers after `stdafx.h` (the MSVC standard headers protect
  themselves from `DEBUG_NEW`).
- **Precompiled headers:** PCH is off. 53 `.cpp` files include other headers
  before `stdafx.h` (`Compiler.cpp` and `FamiTrackerView.cpp` start with
  standard headers), and 65 never include it: all of `APU/`, `Api/`, `Nsf/`
  and `Transcribe/`, plus `Blip_Buffer.cpp`, `FFT/Fft.cpp`, `WaveFile.cpp`,
  `McpSocket.cpp`, `TutorialScript.cpp`, `resampler/`, `ExportTest/` and the
  `CustomExporter*` files. Turning PCH on breaks them.
- **Instrument references:** `CFamiTrackerDoc::GetInstrument()` returns a
  retained pointer. Call `Release()` or use `CInstrumentContainer<T>`.
- **The N163 plays its channels in reverse order:** `CChannelHandlerN163`
  writes channel index i at `0x40 + (7 - i) * 8`, which `CN163` mixes as its
  channel 7 - i (`CHANID_N163_CHAN8` for the first). Code that reaches the
  emulation's N163 channels by `chan_id_t` maps them, as
  `CMixer::StoreChannelLevel` does for the meters and
  `CChannelHandler::SetPan` for the pan.
- **Several expansion chips:** `SNDCHIP_*` are bit flags, and a module can
  have any of them on (on `master`, 0.4.6's dialog takes one). The N163
  channels past its count are registered last, so `GetChannelCount()` still
  counts all eight and a channel index is a column of the module only below
  `GetAvailableChannels()`. The PARAMS block holds the bits, so a file with
  one chip is 0.4.6's. `CCompiler::CompileData` takes one chip: NSF export
  refuses several, and the 5B, which has no NSF driver. The VRC7 is mixed
  raw, as in 0.4.6, so it plays relatively loud beside the other chips.
- **Sunsoft 5B:** its notes use the 2A03's period table, so they sound an
  octave under the note written, as 0CC- and Dn-FamiTracker play them, whose
  5B instruments and `SEQUENCES_S5B` block are saved the same way. `Hxy`
  starts the envelope with shape y, x making its
  period follow the note (C at the note's pitch, 0 for `Ixx` and `Jxx`'s);
  `Vxx` and the instrument's duty sequence set what a channel plays (Vxx: 1
  the square, 2 the noise, 4 the envelope; the sequence: 0x40, 0x80, 0x20,
  the noise's pitch in bits 0-4). The volume column and the volume sequence
  add in 3 dB steps. Until a song uses `Hxy`, `Ixx` or `Jxx`, envelope notes
  play a falling saw at the note's pitch (`HC8`). The three channels share
  the envelope and the noise pitch. Its level is set in `CMixer` against the
  2A03 from rendered measurements.
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
  Such modules don't round-trip through `.txt`. (Fixed on this branch:
  `TextExporter.cpp` reads H, I and J by the channel's chip.)
- **Frames put in at the end:** `InsertFrame` raises the frame count before
  it picks the new frame's patterns, and `GetFirstFreePattern` counts that
  frame's own place, which can still name the pattern of a frame taken out
  (`RemoveFrame` leaves the last place as it was) or undone, so frames added
  at the end one after another got patterns in swapped pairs (02, 01, 04,
  03). (Fixed on this branch: `InsertFrame` picks with `FreePatternForFrame`
  in `Source/Api/FramePatterns.h`, which leaves that place out.)
- **No bounds checks on load:** `CDocumentFile::GetBlockInt`, `GetBlockChar`,
  `GetBlock` and `ReadString` don't check the block size, so a corrupt `.ftm`
  can read past the buffer.
- **Reference-count race:** `CRefCounter::Release()` re-reads the counter after
  `InterlockedDecrement` instead of using its return value, which allows a
  double delete.
- **N163 wave positions:** the N163 wave editor takes positions 0-255, but
  `CInstrumentN163::Load` and text import refuse 128 and above. In Release
  the failed check ends the instrument's load early, so the module loads out
  of step. This branch's loaders (and `Source/Nsf/FtmFile.cpp`) accept 0-255,
  so such modules open, but a wave placed at 128 or later doesn't play as
  drawn either (measured: one wave at 0, 64 or 112 renders identically, at
  128 differently). The Claude Code tools place waves under 128.

## Code conventions

The code is MFC/Win32 C++ of the VS2008 era:
- `C` class prefix and Hungarian members (`m_iChannelID`, `m_pDocument`,
  `m_bPlaying`, `m_cName`).
- Tab indentation.
- `CString`, `TCHAR` and `_T()` for strings.
- `SAFE_RELEASE`/`SAFE_RELEASE_ARRAY`, `TRACE` and `ASSERT`.

New files carry the GPLv2 header used in `Source/*.cpp`.

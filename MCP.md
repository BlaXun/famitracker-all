# Claude Code in FamiTracker

On the `recorder` and `api` branches, FamiTracker can let [Claude Code](https://claude.com/claude-code)
work on the song you have open: read it, write parts, change the key, the
tempo or the sounds, check it for clashes and play it for you. You ask in
plain words ("make the atmosphere darker"); Claude works in bars, beats, note
names and instruments, not hex rows.

It works over MCP (the Model Context Protocol). FamiTracker itself is the
MCP server, so there is nothing else to install besides Claude Code. Claude
Code in WSL connects through `famitracker-mcp.exe`, which comes in the same
zip (see [Claude Code in WSL](#claude-code-in-wsl)).

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

Claude can't hear the song. It checks what it wrote (notes outside the key,
clashes, range) and plays it for you, then asks how it sounds.

## How changes arrive

- **As clips (default).** Claude's ideas are saved as clips, named
  "Claude: ...". The reply says where one goes, for example "on VRC6 Pulse 1
  at bar 5". To hear it: **Edit > Clips**, select it, put the pattern cursor
  there and press **Place a copy**. Ctrl+Z takes it out again. Clips you don't
  want can be deleted in the same dialog.
- **Directly.** With **Let Claude change the song directly** ticked, Claude
  writes into the song. Every change is one step of Ctrl+Z (Ctrl+Y redoes it).
  Claude can then also set the tempo, place clips and undo. Instruments it
  adds from presets stay in the instrument list even when the notes are
  undone.

Without direct changes Claude doesn't change the tempo; when an idea needs
another one, the reply says which speed and tempo to set in the toolbar.

While a dialog is open in FamiTracker, Claude reads but doesn't change
anything; close the dialog and ask again.

## What Claude can do (the tools)

| Tool | What it does |
|---|---|
| `get_song` | Title, tempo in BPM, bars, each channel and what it plays, instruments, clips, the key found from the notes and the chords that fit the melody bar by bar |
| `read_notes` | One channel's notes: bar, beat, note, length in beats, instrument, volume, effects |
| `write_notes` | Writes a part on a channel, replacing its notes in those bars (drums by name on the noise channel: kick, snare, hihat). Speed changes, jumps, skips and halts (`Fxx`, `Bxx`, `Dxx`, `Cxx`) stay, so the song still loops the same way |
| `place_clip` | Places a clip at a bar, as a copy or linked (direct changes only) |
| `add_instrument` | Adds a sound from a preset for a 2A03 or VRC6 channel |
| `set_instrument` | Gives a channel's notes another instrument or preset |
| `transpose` | Moves notes by semitones, or into another key or mode (C major to C minor, lydian, dorian...); arpeggiated chords follow |
| `set_tempo` | Sets the tempo in BPM (direct changes only; undoable) |
| `add_chords_and_bass` | Fits chords to the melody and writes them as an arpeggio, plus a bass line |
| `add_echo` | Copies a part onto a free channel, later and quieter (it stops at the song's end, so the loop stays where it is) |
| `add_vibrato` | Vibrato on the longer notes, starting just after each note begins |
| `change_mood` | Several changes for a mood at once: key or mode, parts up or down, new sounds, vibrato, an echo and the tempo |
| `check_song` | Notes outside the key, clashing channels, notes out of a channel's range, busy and silent bars, unused channels |
| `play`, `stop` | Plays bars for you |
| `undo` | Ctrl+Z (direct changes only) |
| `save_song` | Saves the song, or a copy next to it to compare versions |

Instrument presets: soft lead, bright lead, thin lead, pluck, bell, soft
chords, warm pad, bass, plucky bass, kick, snare, hi-hat, open hi-hat. Each
is built for the 2A03 (also MMC5 channels) and the VRC6.

Notes are written the way FamiTracker shows them: C-4, C#4 (Db4 works too).
FamiTracker's octave numbers are one lower than in most music software: C-3
is middle C and A-3 is 440 Hz. Bars and beats count from 1, as the row
highlight shows them (the first highlight is a beat, the second a bar).
Claude works on the track that's selected.

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

## Safety

- FamiTracker only listens on 127.0.0.1, so other computers can't reach it.
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

## Not yet

- Exporting a looping OGG for game engines (FamiTracker 0.4.6 exports WAV).
- Recording ideas from a phone page into the clips.
- Other tracks than the selected one, and presets for FDS, N163, VRC7 and 5B.

## For developers

- `Source/Api/` is plain C++14 without MFC, built and tested on Linux:
  `Json` (reader and writer), `Http` (request framing), `Mcp` (the protocol:
  per-request metadata of 2026-07-28 and the initialize handshake of
  2025-03-26 to 2025-11-25, Host, Origin and token checks), `Music` (cells,
  note names, bars and beats, keys and modes, writing a part into a range of
  rows), `Presets`, and `Tools` (the tools, against the `SongHost` interface
  in `Song.h`).
  ```
  cmake -S Source/Api -B build/api
  cmake --build build/api
  ctest --test-dir build/api --output-on-failure
  ```
  The tests run every tool against a song in memory and the protocol with
  the requests real clients send. The protocol was also checked against the
  official MCP TypeScript SDK clients (1.x, and 2.x in both negotiation
  modes).
- `Source/McpSocket.cpp`: the listener thread (Winsock, no MFC). It frames
  HTTP requests and hands each to the UI thread with `SendMessageTimeout`.
- `Source/Api/Bridge.cpp` and `Source/Api/tools/famitracker-mcp.cpp`: the
  connection for Claude Code in WSL, an MCP server over stdio. Each JSON-RPC
  line becomes the HTTP request Claude Code would send (with the protocol
  headers the endpoint checks) and each answer a line; an answer refused
  before it was read (a wrong access code) still goes to its request.
  `BridgeSocket.cpp` sends it to 127.0.0.1 (Winsock, or BSD sockets for the
  tests). On Windows it reads the port and the access code from
  `HKEY_CURRENT_USER\Software\FamiTracker\Claude Code` for every message;
  `--port` and `--token` override them. The tests also run it against the
  endpoint over real sockets.
- `Source/McpConnection.cpp`: `SongHost` over the open document, the
  message-only window that answers on the UI thread, the settings (registry
  section "Claude Code") and the dialog. Song changes go through
  `WriteSongParts` in `InsertRecording.cpp`, one undoable action each.

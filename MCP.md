# Claude Code in FamiTracker

FamiTracker can let [Claude Code](https://claude.com/claude-code) work on the
song you have open: read it, write parts, change the key, the tempo or the
sounds, check it for clashes and play it for you. You ask in plain words
("make the atmosphere darker"); Claude works in bars, beats, note names and
instruments, not hex rows.

It works over MCP (the Model Context Protocol). FamiTracker itself is the
MCP server, so there is nothing else to install besides Claude Code. Claude
Code in WSL connects through `famitracker-mcp.exe`, which comes in the same
zip (see [Claude Code in WSL](#claude-code-in-wsl)). Other MCP clients on
another computer of your home network, such as an AI assistant on a Linux
PC, can connect too once you allow it (see
[Connecting from another computer](#connecting-from-another-computer)). Your own model can be asked from
FamiTracker itself, with the same tools, in View > Ask (see
[Your own model inside FamiTracker](#your-own-model-inside-famitracker-view--ask)).

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

## Connecting from another computer

FamiTracker can serve its tools to another computer on your home network
too: an AI assistant on a Linux PC in the next room, say, or a laptop. It's
for your home network and Tailscale only, never for the internet.

### Turning it on

1. In FamiTracker: **File > Connect Claude Code...**, tick **Let Claude Code
   connect**.
2. On the right, under **Other computers on your network**, tick **Allow
   connections from other computers on my network**. The column shows this
   computer's addresses and the access code, each with a **Copy** button:

   ```
   Ethernet:     http://192.168.1.10:46600/mcp        [Copy]
   Tailscale:    http://100.101.102.103:46600/mcp     [Copy]
   Access code:  1f3a...                              [Copy]
   ```

3. Press OK. Windows asks once whether FamiTracker may use the network
   ("Windows Defender Firewall has blocked some features of this app"): tick
   **Private networks** and press **Allow access**.
4. On the other computer, give its MCP client the address and the access
   code, as the header `Authorization: Bearer <code>`.

Take the address of the network both computers are on: the home network's
(192.168..., 10..., 172.16-31...) at home, Tailscale's (100...) wherever
your Tailscale reaches. This computer's name works too, where the other
computer can look it up (`http://desktop-pc:46600/mcp`, or Tailscale's
`http://desktop-pc.your-tailnet.ts.net:46600/mcp`). FamiTracker remembers the
setting; untick it and FamiTracker listens on 127.0.0.1 only again, as
before.

### A small set of tools for local models

Local models (a Qwen running in llama.cpp, say) do better with fewer tools
and shorter instructions than the 60 or so at `/mcp`, and they follow one
clear step better than a list of eight tips. `/mcp/core` serves 43 of them:
`what_next`, the tools its steps call, `explain`, the agent log's two and the backups' three. Reading and writing: `get_song`,
`read_notes`, `write_notes`, `place_clip` (ideas hummed into the phone or the
microphone are clips); sounds: `add_instrument`, `design_instrument`,
`find_sound`, `reference_sound`, `set_instrument`; parts:
`add_chords_and_bass`, `grow_melody` (a hook grown into phrases, in versions to
pick from), `add_drums`, `add_harmony`, `add_echo`; the form:
`vary_part`, `copy_bars`, `insert_bars`, `name_section`, `song_shape`, `set_loop`; the
frame: `set_key`, `set_tempo`, `set_swing`; finishing: `polish`, `mix`,
`check_song`, `review_arrangement`, `check_loop`, `fix_loop`, `listen` (the
song heard as FamiTracker plays it), `finish_song` (the last of them in one
call); and `play`, `stop`, `save_song`,
`export_for_game`, `get_recipe` and `explain` (the glossary's words for a
term, to explain things to you in), `write_log` and `read_user_notes` (see
[The agent log](#the-agent-log-why-and-notes-back)), and `create_backup`,
`list_backups` and `restore_backup` (see [Backups](#backups-going-back-in-one-step)). Not `set_mixer`:
`mix` sets the volumes in the notes (a local model once used the faders as
its mix). Its instructions are a page instead of four, built around a
loop: ask `what_next`, make the call it gives, ask again until it says done
(see [One step at a time](#one-step-at-a-time-what_next)). They keep the
craft (a motif in phrases that breathe, written in steps; chords and a bass
in a style; sounds found or designed) and the rules that decide how it
sounds: notes on a sound that's gone at once vanish, a vibrato on the
2A03's pulses can click, and only 0.4.6's effects play. It serves no
prompts, so a client adds no tools of its own to list them; `get_recipe`
gives the recipes for calm, lively and action songs instead: the loop, the
plan it follows in this set's tools, and a worked melody of 8 bars in
steps over its chords. Its `check_song` and `review_arrangement` answer in
a few lines unless a `format` is asked for (see [Short replies](#short-replies)),
so a long reply doesn't push the song out of a small context. Tick **A small set of
tools, for local models** in the dialog and its addresses end in
`/mcp/core`. Both sets are always there; each client picks one by its
address.

### Hermes Agent

[Hermes Agent](https://hermes-agent.nousresearch.com/docs/user-guide/features/mcp)
keeps its MCP servers in `~/.hermes/config.yaml`, under `mcp_servers`. Over
HTTP, with the address and access code from the dialog:

```yaml
mcp_servers:
  famitracker:
    url: "http://192.168.1.10:46600/mcp/core"
    headers:
      Authorization: "Bearer 1f3a..."
    timeout: 600          # export_for_game renders the song first
    connect_timeout: 30
```

Then `/reload-mcp` in a Hermes session (or start a new one). The tools show
up as `mcp_famitracker_get_song` and so on. To keep the code out of the
file, put `FAMITRACKER_TOKEN=1f3a...` into `~/.hermes/.env` and write
`Authorization: "Bearer ${env:FAMITRACKER_TOKEN}"`. Use `/mcp` instead of
`/mcp/core` for every tool, with a model that copes with them. Hermes in a
Distrobox container shares the host's network, so the same address works
there.

For clients that only start MCP servers over stdio, the zip has
`famitracker-mcp-linux` (Linux, x86-64, linked statically, so it runs in any
distribution or container). Copy it to the Linux computer, make it
executable (`chmod +x famitracker-mcp-linux`) and let the client start it:

```yaml
mcp_servers:
  famitracker:
    command: "/home/you/bin/famitracker-mcp-linux"
    args: ["--host", "192.168.1.10", "--token", "1f3a...", "--core"]
    timeout: 600
```

`--host` takes the address or the computer's name, `--port` another port
than 46600, and `--core` picks the small set (leave it out for all the
tools). It passes each message to FamiTracker over the network, like
`famitracker-mcp.exe` does for WSL, and says what to check when FamiTracker
doesn't answer.

For "export as OGG and give it to me" from a phone, Hermes needs no more than
the setup above (a `timeout` long enough for a render) and one of the ways in
[Files between the computers](#files-between-the-computers): the link, when
the phone reaches the PC (Tailscale on both, or the home Wi-Fi); otherwise its
terminal fetches the file with curl from the link (no header needed), or it
reads the resource, and it sends the file on through its chat.

### Files between the computers

`study_nsf`, `import_nsf`, `study_module` and `make_sound_effect` (a voice
recording) read files on the PC FamiTracker runs on. From another computer,
send the file first: `send_file` takes its name and its bytes in base64 and
puts it into the **Inbox** folder next to FamiTracker.exe
(`Documents\FamiTracker inbox` where FamiTracker's folder can't be
written); the reply gives the path to pass on, like
`C:\Users\you\FamiTracker\Inbox\Mega Man 2.nsf`. A client with a shell
sends the file itself, which is quicker:

```
curl -T "Mega Man 2.nsf" -H "Authorization: Bearer 1f3a..." "http://192.168.1.10:46600/inbox/Mega%20Man%202.nsf"
```

The Inbox takes NSF and NSFe files with their `.m3u` playlists, MOD, S3M,
XM, IT and MIDI files, FamiTracker modules and WAV recordings, at most 5 MB
each; a file of the same name is replaced. Its name can't hold folders.

`export_for_game` saves its OGG files next to the song, on the PC. A song
that was never saved is saved first when a client asks, since it can't use
File > Save As: into the **Songs** folder next to FamiTracker.exe
(`Documents\FamiTracker songs` where FamiTracker's folder can't be written),
in a folder of its own named after the track (`Songs\Town\Town.ftm`, else
the module's title or `Untitled`; `Town 2` when the name is taken, never over
a file). From then on the song is that file, and its OGG files go next to it.
`save_song` does the same, with `name` for the song's name, and `what_next`
gives a client that call (with the track's name) where it would otherwise ask
you to save. FamiTracker's own File > Export for games and its What next panel
still ask you to save first.

Asked over the network, the reply says how to get each file to the person,
three ways, for clients that can do different things:

- **A link that opens in a browser.** Each file's link carries a code of its
  own in the address, so a browser opens and plays it with no header, a
  phone's too, on the home network or over Tailscale: pass the link on as it
  is. When the PC has a Tailscale address and the client reached it another
  way, the reply gives that link too, for a phone away from home. The code is
  worked out from the access code (as the phone page's is), opens only the
  OGG files next to the song, and changes with a new access code. A browser
  gets the range it asks for (206 Partial Content), as iPhones' Safari needs
  to play a sound.

  ```
  http://192.168.1.10:46600/files/Town.ogg?code=8c41e0d2f7a9b3c5d6e7f801
  curl -o Town.ogg "http://192.168.1.10:46600/files/Town.ogg?code=8c41e0d2f7a9b3c5d6e7f801"
  ```

  The access code itself never goes in an address: `?code=` refuses it. The
  link without its code still works with the access code in the header, as
  before.
- **MCP resources**, for a client that fetches files without HTTP, through
  `famitracker-mcp-linux` too: the OGG files next to the song are the
  resources `famitracker://files/<name>`, listed by `resources/list`, and
  `resources/read` gives a file's bytes in base64 (a `blob`, `audio/ogg`).
  The reply links each file it saved as a resource (`resource_link`) for
  clients of the protocol's 2025-06-18 version or later (those that send
  `MCP-Protocol-Version`, as `famitracker-mcp` now does after the handshake);
  2025-03-26 has no links, but its clients read the resources all the same.

  ```json
  {"jsonrpc":"2.0","id":7,"method":"resources/read","params":{"uri":"famitracker://files/Town.ogg"}}
  ```
- **Attached to the reply**: `export_for_game` with `attach` true carries the
  files themselves (embedded resources, `audio/ogg`), at most 8 MB in all, for
  a client that saves what a reply carries; the reply names them with their
  sizes, and those it left out. Not for a client that shows replies to its
  model: the file in base64 would fill its context.

So "export as OGG and give it to me" is one `export_for_game` call on any
song, saved or not; then the link goes to the person, or the client fetches
the file (the link with curl, or `resources/read`) and sends it on.

### A new song, and back to an old one

Every tool works on the song open in FamiTracker. `new_song` starts another
in its place, as File > New does (with the game music profile, its
template: the VRC6, 120 BPM and its sounds), without a dialog, so it works
with nobody at the PC. `expansion` picks other chips, as `set_expansion`
takes them (`["VRC6 Plus"]` with its soft layer's sounds, `["FDS"]`, `[]`
for the 2A03 alone; the template's VRC6 sounds go when the VRC6 does), and
`name` is the module's title (its first 31 letters) and its track's name.
Asked over MCP, the new song is saved at once into the **Songs** folder
under its name (`Songs\Town\Town.ftm`; `Town 2` when that's taken), so
`save_song` and `export_for_game` work on it straight away; asked from
FamiTracker's own Ask panel, it stays untitled, as File > New leaves it.

`open_song` opens one again by name: the songs in the Songs folder and
FamiTracker's recent files (File menu). `list` true (or no name) names them,
the newest first, 30 at most; a word of a name does when only one song has
it. From this computer, `path` opens any other `.ftm` file (a drive, folders
and the file, nothing else); from another computer it takes names only.
FamiTracker reads the file first, so one it can't open is refused with the
reason rather than with a message box nobody may be there to close.

Both refuse, changing nothing, while the open song has changes that aren't
saved: `save_song` first (with `name` for a song that was never saved), or
ask the person. The song that was open stays in its file as it was last
saved; its Ctrl+Z history goes with it.

From a phone (Hermes on `/mcp`: neither tool is in the small set), the
whole round is:

- "Start a new song called Town": `new_song` with `name` "Town" (and
  `expansion` when asked), then `what_next` for the first step.
- "Save it": `save_song`.
- "Start another": `new_song` again, once the first is saved.
- "Go back to Town": `open_song` with `name` "Town".

### The phone page

Edit > Record from your phone serves a page for recording ideas on a phone
(see `RECORDER.md`, Ideas from your phone) at `/phone`, on the same port and
with the same checks as the routes above. Its routes take a code of their own
instead of the access code: `/phone?code=CODE` in the address the window's
QR code carries, or `Authorization: Bearer CODE`. The code is worked out from
the access code (the first 24 hex digits of its HMAC-SHA-256), so it opens
only these routes: the access code doesn't open them, and their code doesn't
reach `/mcp` or the files.

- `GET /phone`: the page.
- `GET /phone/takes`: the takes sent so far, newest first, as JSON: `takes`,
  each with `idea`, `state` (`waiting`, `clip` or `failed`), `clip`,
  `message`, `seconds`, `arrived` and `file`; and `waiting`.
- `POST /phone/take?name=NAME`: a take, the request's body a WAV file of
  16-bit PCM on one channel at 8000-48000 Hz, half a second to 3 minutes
  (8 MB at most). The answer (202) has the take; it becomes a clip of the open
  song a moment later, named after the idea, and `place_clip` takes it by
  that name.

A client with a shell sends a take itself:

```
curl -H "Authorization: Bearer CODE" -H "Content-Type: audio/wav" --data-binary @idea.wav "http://192.168.1.10:46600/phone/take?name=Town%20melody"
```

### Over the network, safely

- FamiTracker only answers addresses of home networks (10.x, 172.16-31.x,
  192.168.x, 169.254.x) and Tailscale's (100.64-127.x), besides this
  computer; any other address is closed off at once.
- Every request needs the access code, files too, except the links
  `export_for_game` gives: their own code, in the address, opens only the OGG
  files next to the song. Web pages are turned away as before.
- The phone page is the one web page FamiTracker answers, and only its own
  requests, with its own code; other pages can't send it takes.
- Never forward the port on your router: from outside, use Tailscale.
- **New access code** in the dialog makes the old one stop working on the
  other computer too: give it the new one. The phone page's code changes with
  it: scan the QR code in Edit > Record from your phone again.

## Your own model inside FamiTracker: View > Ask

View > Ask is a question box in FamiTracker itself, for your own model:
any server that speaks OpenAI's chat API on this PC or your home network,
such as llama.cpp's `llama-server`, llama-swap, Ollama or LM Studio. Nothing
proprietary is built in or needed, and nothing is sent until you press
Send. The model gets the same tools Claude Code gets here (all of them, or
the small set for smaller models), runs them inside FamiTracker, and
answers in plain words, with the glossary's words as links ("a
[[chord]]" opens Help > Glossary at Chord).

The panel docks along the bottom, beside What next and the problems panel,
or on the right (right-click it). Type a question and press Enter (Shift+Enter
for a new line). The answer streams in as the model writes it; Stop ends it
at once, and New question starts over. Each song keeps its own conversation
while FamiTracker runs. With each question the model is told the song as it
is: `get_song`'s overview, the step View > What next would take (for the
kind of song you chose there, which the model is asked to keep: it gives
`what_next`, `get_recipe` and `finish_song` that `kind`), the exercise you're
doing (Help > Exercises), and the bar and channel the cursor is on.

### Ask first, or let it work

- **Ask first** (the usual mode): what only reads the song (`get_song`,
  `read_notes`, `check_song`, `play`...) runs at once. A call that would
  change the song shows as a proposal, in words with the call under it:
  **Do it** makes it (one Ctrl+Z, as What next's Do it), **Skip** tells the
  model you'd rather not. A question may take 8 rounds of tools.
- **Let it work**: for "compose a calm night song in D minor" or "add drums
  to bars 5-12". The changes go in as the model makes them, each one its own
  Ctrl+Z, and the panel lists what each step did. **Take back all** undoes
  the whole run (as long as nothing was changed after it), and you can listen
  while it works. A question may take 300 rounds (a whole song is dozens of
  calls, `what_next`'s loop).

Both limits are in the settings. Which tools only read is a list kept in
step with the tools (`AssistantToolKinds` in `Source/Api/Assistant.cpp`).

### Setting it up with llama.cpp

Start `llama-server` with `--jinja`: that's what lets the model call tools
(the chat template's tool calling). Without it the server refuses tools
("tools param requires --jinja flag"), the panel says so, and the model
answers from the song's summary alone, without reading or changing the song.
Give it room. Measured as the panel sends them, all of FamiTracker's tools
and their instructions are 170 KB, about 45,000 tokens (3.5-4 bytes a token),
and the small set 94 KB, about 25,000: give the model a context of 64K tokens
or more for all of them (`-c 65536`, better `-c 131072`) and 32K or more for the
small set. The panel learns the real count from the server's first reply.
`--host 0.0.0.0` lets this PC reach a server on another computer. For example,
on the Linux PC:

```
llama-server -m Qwen3.6-35B-A3B-Q4_K_M.gguf --jinja -c 131072 --host 0.0.0.0 --port 8081
llama-server -m gpt-oss-120b-mxfp4.gguf --jinja -c 131072 --host 0.0.0.0 --port 8080
```

In the panel's **Settings...**, the address is the server's, like
`http://192.168.1.50:8081` or `http://strix:8081` (the `/v1` is added; plain
http, as on your home network). Leave **Model** empty for `llama-server`.

- **llama-swap**: the address is llama-swap's port, and **Model** is the
  name in its config (Test connection lists them; empty picks the first it
  lists). Put `--jinja` and the `-c` in each model's `cmd`. The panel reads
  the context through `/upstream/<model>/props`.
- **Ollama**: `http://<computer>:11434` (its OpenAI-compatible `/v1`) and
  the model's name; pick a model that can use tools. Ollama has no `/props`,
  so the panel counts on 32K tokens unless you give the context in the
  settings. Raise Ollama's context: its default is small, and it cuts a
  longer question without a word (the model then misses the tools and the
  song; the panel says so when the server's count shows it). Start it with
  `OLLAMA_CONTEXT_LENGTH=65536 ollama serve`, or use a model made with
  `PARAMETER num_ctx 65536` (the `/v1` endpoint takes no `num_ctx` of its
  own).
- **LM Studio**: start its server (`http://<computer>:1234`) and load a model
  that can use tools.
- **Tailscale**: the MagicDNS name or the `100.x` address works like a
  home address.
- **Key**: only if the server wants one (`llama-server --api-key`); it's
  kept encrypted for your Windows account (DPAPI), never as plain text.
- **Tools**: all of them (the default, for big models with a big context)
  or the small set (`/mcp/core`'s 43, with its short instructions and short
  replies) for smaller models, or a context under 64K.
- **Context**: 0 takes what `llama-server` reports (`/props`); give a number
  for a server that doesn't say. Older tool replies are shortened, then
  older questions left out, before a conversation would overflow it.

Each request starts with what never changes, byte for byte the same every
time: the system prompt and the tool definitions (built once, in the tools'
fixed order). The song's state comes after them, in your question's message.
So the server keeps what it has read and reads only what's new on the next
question or tool round: `llama-server` does by default (add `--cache-reuse
256` to reuse more), Ollama while the model stays loaded (`OLLAMA_KEEP_ALIVE`).

**Test connection** reaches the server, lists its models, reads its context
and asks a word with one tool, so it says whether tools are on. The first
question to a big model takes a while: the model reads all the tools once
(llama-swap may load the model first); after that the server reuses what it
read, and the panel counts the seconds meanwhile.

Models that use tools well: the Qwen3 family and later (Qwen3-30B-A3B,
Qwen3.6-35B-A3B), gpt-oss-20b and gpt-oss-120b, GLM 4.5 Air and later,
Mistral Small 3.2. Small models (under about 8B) often call tools badly:
give them the small set and keep to Ask first.

### Privacy, and when something goes wrong

The questions, the song's summary and the tools' replies go to the address
in the settings, and nowhere else. When that address isn't this PC or on
your home network (the ranges FamiTracker's own network connection allows:
10/8, 172.16/12, 192.168/16, 169.254/16 and Tailscale's 100.64/10), the
panel says so above the conversation.

A server that's off, slow or answering badly gets a plain message with what
to check, never a hang: nothing listening at the port ("is the server
running, is that its port, does it listen on the network?"), a computer
that doesn't answer (on? a firewall?), no model of that name, no chat at
that address, a conversation longer than the context, or a model silent for
five minutes. Stop always works.

With no model set up, the panel searches FamiTracker's own help instead:
the glossary, the exercises and the tutorials, each a link.

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
- "Make a night version of this song." / "Make the night version a bit less
  slow and keep the drums."
- "Make a battle version." / "Make a level-up jingle and one for finishing a
  quest."
- "Does the song loop cleanly?" / "Let bars 1-4 be an intro and loop the rest."
- "Export the day and night versions for my game."
- "Let's make a VRC7 song." / "Give the melody a piano sound."
- "Make it faster, speed 5." / "Set the tempo to 100 BPM."
- "Make a jump sound and three coin sounds to choose from." / "Turn my
  recording pew.wav into a sound effect."
- "Start a battle theme in D minor." / "Give me a groove for a spooky cave."
- "Use these chords: | Am7 | Fmaj7 | C | G7sus4 G7 |, as broken chords, with a
  walking bass."
- "Add drums with a fill every four bars." / "Give the melody a harmony a
  third below." / "Add a counter-melody under it."
- "How full does the arrangement sound? What's missing?"
- "Make a lead that swells in and gets a vibrato on long notes."
- "Repeat bars 1-8 at bar 9, but make the repeat different."
- "Remember that I like bright leads and don't want echo on the melody."

Claude hears the song with `listen`: FamiTracker renders its real audio,
and the tool reports what an ear would notice (warbles, notes out of tune,
clicks, a covered melody). It also checks what it wrote (notes outside the
key, clashes, range), looks at a picture of the notes, reviews how full the
arrangement is, and plays it for you, then asks how it sounds: your own ear
has the final word.

Four recipes come with the connection as commands in Claude Code (type `/`
to see them): `/mcp__famitracker__compose_track` composes a track for a
scene ("a snowy village at night") step by step, `/mcp__famitracker__deepen_track`
works through what makes the open song sound plain,
`/mcp__famitracker__make_versions` makes versions of a part as clips to
choose from, and `/mcp__famitracker__study_track` explains what makes a track
work, to reuse its ideas.

`compose_track` takes a `kind` too, for a recipe of its own:

- **calm**, only when you ask for a calm song (a place to rest, a night
  scene, a lullaby). Its melody never presses: a lead that swells in, falls
  low after its strike or fades away like a music box, only 2-3 dB over the
  chords. It plays few, long notes by step over slow, colored chords, with
  no drums.
- **lively**, for a stage to explore. It bounces and drives, the way the
  soundtrack's first lively stages were made: a lead that blooms in and
  settles, a step louder on a bar's first note and at the climax, inside a
  band louder than it (the bass about level with the melody), a sampled
  kick and snare, and sections that build to a climax 2-4 dB over the
  first A.
- **action**, for fights, bosses and chases. These stay punchy: the melody
  on top, a backbeat, accents.

Each recipe ends with its numbers from the built-in guide (topics `calm`,
`lively` and `action`), so Claude reads them once instead of looking them
up. Without a kind, the general recipe points at the three when your idea
asks for one.

### Arguments a tool doesn't take

A call with an argument the tool doesn't take does nothing and says so,
naming the argument it likely meant and the tool's own (the required ones
marked), so a model corrects the call: `find_sound` with `words` answers
*find_sound has no argument "words" (did you mean "query"?), so nothing was
done. Its arguments: query, kind, from, channel, other_chips.* Such an
argument used to be ignored, so `find_sound` listed every sound (2,129 for
one song) and `transpose` given `channel` moved every channel. The argument meant is
named when it's the same written another way (`BPM`, `fromBar`), a letter or
two off (`channel` for `channels`), a word of its name (`motion` for
`design_instrument`'s `duty_motion`) or the only one whose description starts
with the word (`query`, "Words to look for"). Every call the recipes, the
instructions, `what_next` and the checks' fixes give, and every call
FamiTracker makes itself, uses only the tools' own arguments (the tests read
them all).

### One step at a time: `what_next`

`what_next` looks at the open track and answers with the one step that
matters most now: the tool to call, its arguments as JSON ready to send, and
a sentence of why; then what's done and what's left. It works it out from
the song itself (its notes, sections, key, loop, mixer and the game's file
next to it), not from the calls before, so it gives the same answer after a
restart, and a model can follow it a step at a time: ask, make the call,
ask again, until it says the song is done and to play it for you. Its order
is the recipes':

1. What breaks the sound: `check_song`'s findings that come with a fix
   (notes that click, notes that vanish, notes out of range, and clashes
   worth fixing: one `polish` with the touch `clashes` for the whole song,
   see [Clashes](#clashes)). Its other
   findings with a fix come later: the melody in one-bar pieces, a thin or
   doubled bass and a sound its channel can't play once the parts are
   there, before depth; faders doing the mix, a copied repeat and a short
   loop with the review's tips. A fix of several calls (read the melody in
   steps, then write it back held over the bar lines) comes as its first
   call, with the calls after it, in order, in the why. Clashes are listed
   for you to hear.
2. The frame: an even tempo in the kind's range, the key, and on an empty
   song its shape (`song_shape`: a lively stage for lively, a calm night for
   calm, a boss fight for action, a town otherwise, or the short loop when
   the track's name asks for one, like "Shop" or Help > Exercises' "A first
   small song", while a town's or a village's name keeps the town, "Home
   village" too; see [Song shapes](#song-shapes)), so every part has its
   place and its job first.
3. The parts and the form: the lead's sound, an A part's chords and bass,
   its sections named, the melody, A again, a B part on other chords, A
   once more, and drums (none for calm; a sampled kick and snare under
   noise hats otherwise). On a song laid out from a shape the parts fill
   its sections in order (A' takes A again, B chords of its own, the climax
   or A'' A once more, a boss fight's climax chords of its own), so no bars
   are put in or named afterwards. A lively stage's lead blooms in and settles
   (`bloom lead`, not struck again) and its chords are lush arpeggios of
   two frames a note. A calm song's lead is the night songs' best, as the
   calm recipe has it: a music box on a VRC6 pulse at 37.5% (50% on the
   2A03), blooming in over two frames without a duty click and gone within
   about a beat, never holding a level (the breath is the other way, as
   `get_recipe`'s calm steps say on the small set too). A part's melody is `grow_melody`
   with a hook: the song's own (its first notes in A) where it grows
   without a clash, else one offered on the chords there, which a composer
   replaces with a hook of its own; the model then picks the version whose
   shape fits and shapes it by hand. Where a hook can't grow (fewer than
   four bars), a phrase to start from comes as `write_notes`.
4. Depth: a harmony in the repeat (cleaner beside the chords than a
   counter-melody), the repeats made different (`vary_part`, a variation
   that rubs against nothing), a break and an intro that plays once, filled
   with the chords and the bass, and the loop after the intro.
5. The review's tips that suit the kind (a lively stage that doesn't build
   gets a harmony in its last part, the climax).
6. An echo, last of the parts, since it copies the melody's notes as they
   are.
7. The last steps, once the parts are written: `polish` (on a lively stage
   with its accents), then the mix in the kind's style when another mix
   would change the song, the loop's seam (`fix_loop`), a `listen` and the
   file for the game at the kind's loudness (-20 LUFS; -22 calm, -19 action),
   as one `finish_song` call from the first of them that's due (see [The last
   steps in one call](#the-last-steps-in-one-call)): polish isn't asked again
   once the song is mixed, nor the mix when only the seam is left. The
   finish's listen (the song heard as FamiTracker plays it) counts while the
   song stays as it was heard, and its high findings' fixes come next, as
   `check_song`'s do. Then saving. The file alone, older than the song or at
   another loudness, is `export_for_game`, with a `listen` before it when
   the song changed since it was heard (a fix too).

It takes `kind` (calm, lively or action; without one, calm when the track's
name says so, else the general recipe), `skip` (steps to leave out, like
`["drums"]`, or a kind of `check_song`'s finding) and `all` (every step found
now, in order). With direct changes off it asks for them first, since each
step works on what the one before wrote. `export_for_game` (and
`finish_song`'s export) notes in each file what it was made from (a print of the song as it sounds and the
loudness, as the `FAMITRACKER_PRINT` and `FAMITRACKER_LOUDNESS` comments), so
`what_next` knows when the file is older than the song.

With `format` `json` it answers with the step as data, for programs
(FamiTracker's View > What next reads it): `step`, with its `id` (as `skip`
names it; `done` once every step is), `what` (what to do in plain words,
like "Set the tempo to 150 BPM"), `stage` (one of the recipe's: `frame` (the
tempo, the key and an empty song's shape), `parts`, `form`, `depth`,
`polish`, `mix`, `loop`, `export`; null for a fix of `check_song`'s, which
comes before them, and for the end), `tool` and `arguments` with `call`
exactly as the text gives it (or "ask the user", with `ask_user`), `why`,
`own_notes` (a melody's step that offers a hook or a phrase to start from,
where a tune of the user's own is better), `calls` (more than 1 when calls
follow it, given in the why), a fix's `severity`, and where it works:
`from_bar`, `to_bar`, `channel` and `channel_name`; `steps` (every step
found, with `all`); `done`, `done_count` and `total` (the milestones the
text counts as "5 of 20 done", a count that never falls as the steps are
made: the melody is the first part's, a later part's melody is the form's,
and depth waits for the form's repeats); `milestones` (each `name`, `stage`, `done`
and what's done, `text`); `stages` (each `id`, `name`, and how many of its
milestones are `done` `of` how many); `notes`; `kind` and `kind_note`; and
`again`, the arguments to ask again with.

## How changes arrive

- **As clips (default).** Claude's ideas are saved as clips, named
  "Claude: ...". The reply says where one goes, for example "on VRC6 Pulse 1
  at bar 5". To hear it, press the play button on its card in the clips
  panel beside the song; to use it, drag the card there in the pattern
  editor. Ctrl+Z takes it out again. Right-click a clip you don't want to
  delete it.
- **Directly.** With **Let Claude change the song directly** ticked, Claude
  writes into the song. Every change is one step of Ctrl+Z (Ctrl+Y redoes it).
  Claude can then also set the tempo, place clips and undo. Changes leave
  the song's speed changes, jumps and halts (`Fxx`, `Bxx`, `Dxx`, `Cxx`)
  where they are, unless moving them is the change (`set_loop`,
  `insert_bars`, `remove_bars`); a clip
  Claude places keeps the ones where it lands, as when you drag one. Instruments it
  adds from presets stay in the instrument list even when the notes are
  undone. FamiTracker plays only the effect columns a channel shows, so
  where Claude's effects need more room (a vibrato beside a fade, a scoop
  beside a delay), the channel shows more of its four, as the + button over
  it would; the reply names those channels, and Ctrl+Z shows the old number
  again.

Without direct changes Claude doesn't change the tempo; when an idea needs
another one, the reply says which speed and tempo to set in the toolbar.

While a dialog is open in FamiTracker, Claude reads but doesn't change
anything; close the dialog and ask again.

### Clicks in held notes on the 2A03's pulses

The 2A03's two pulses (and the MMC5's) restart their wave whenever
FamiTracker writes a new high byte of a note's period, which it does each
time that byte changes. A vibrato (an instrument's built-in one, or `4xy`)
that takes a held note's period across a multiple of 256 therefore clicks
every time it crosses, several times a second: A-3 (period 253) does with
the gentlest vibrato, F-1 (1280) with any, and A-2, D-2 and A-1 with a deep
one. The VRC6's pulses and the triangle don't restart their waves.

- When Claude changes the song directly, a note it writes that would click
  is nudged a period step or two sharper or flatter with `Pxx` (a few
  cents, at most 15), and the next note gets the fine pitch back (the
  loop's first note too, when the nudged note is the song's last). The
  reply names the notes, for example "A-3 at bar 5 beat 1 (P81, 7 cents
  sharp)". A clip isn't nudged, since its neighbours are the song's once it's
  placed; the reply says which of its notes would click.
- `check_song` lists the notes that click: ones written before this, or
  ones an instrument's vibrato reaches after it changed. `polish` with the
  touch `clicks` nudges them, and `export_for_game` says when a track it
  exported still clicks.
- A vibrato too deep to nudge clear is named in the reply, so a gentler one
  or another channel can take the note.

### Notes that vanish

A note on a sound that's gone within a few frames (a pluck falling 15 10 5
2 1 0) comes out as a blip. Among held notes of the same line it sounds like
a missing note with a tick in its place: a user heard it at once in a tune
that went from a held lead to such a pluck and back. A tune's notes need a
sound that lasts, such as a pluck that settles to a level
(`design_instrument` kind `pluck` with sustain `half`: 15 down to about 9) or
a click pluck that holds low.

- `check_song` lists the notes of a line that vanish between its held
  notes: a run of up to four, with a held note of the line just before and
  just after, less than an octave away. A stab an octave off the line (an
  oom-pah's), a longer run (ticks, a ratchet) and a run where a line starts
  or ends are figures of their own and left alone, and so is a sound struck
  again while the note lasts (an envelope that loops back up, an echo).
  `check_song` lists the faults heard worst first (notes that vanish,
  clicks, clashes), so a long list of notes outside the key can't hide them.
- `review_arrangement` counts such notes in the melody against the score,
  and a melody whose notes mostly vanish too.
- `design_instrument` and `reference_sound` say so when the sound they add
  is gone within four frames.

### Clashes

`check_song` hears a clash where two parts sound a semitone apart (or a
semitone and octaves) for half a beat or more. It's either worth fixing at
once (`severity` `high`: held a beat or more, on a strong beat, or two chord
parts that disagree, a pluck's C# under pads and a tune on D) or a detail
that may be meant (`low`, with `detail` `passing`: a passing note shorter
than a beat against a chord's tone; or `color`: a tone of a chord's
arpeggio, as lush chords color a tune). It's harmony: `listen` judges how
the song sounds, not that, so it doesn't hear them.

- `polish` with the touch `clashes` moves the accompaniment's note onto the
  chord the other parts play there: the nearest of its notes that rubs
  against nothing, with the note's instrument, volume, effects and length
  (an arpeggio keeps its shape). The melody never moves, nor does a note
  that slides, a lush chord's tone or the bass's root; the bass moves only
  where its own note disagrees. The note no other part plays goes first;
  where that's the melody's, the chord voice under it. A held note moves for
  its whole length only while the chord keeps its third (it isn't the third,
  or another part plays it too; for a passing note's clash a held third
  stays), and the chords the parts play keep their names, but for the color
  a clash worth fixing made (a held F# under the tune's G turns Gmaj7 into
  G). It goes over the song again until nothing moves, so polishing again
  changes nothing, and it says which clashes stay and why.
- Each clash it can clear carries that call as its `fix` (over the bars of
  its two notes, on its two channels, with `melody_channel`): the problems
  panel's **Fix** makes it, and lists the details apart ("Clashes: details,
  maybe meant"). `what_next` gives one `polish` with `touches`
  `["clashes"]` for the whole song once a clash worth fixing has a fix;
  details alone are no step. `write_notes` warns as it writes a part that
  clashes with the others held or on a strong beat where that touch would
  clear it, with the call over the part's bars.
- A local model's calm song had 46 clashes; one call left 14: 2 worth
  fixing (the tune held on D over a pad's C#, the third of A no other part
  plays) and 12 passing notes over the pads' held thirds. The finished
  soundtrack's 243 clashes are tones of lush arpeggios but 4, and the touch
  moves none of its notes.

### What drafts miss

A local model's first stage was clean on every check above and still
sounded like a draft: its melody came in one-bar pieces, two channels
thumped the bass's roots, the triangle played a soft pulse sound, the
faders did the mix's work, a section came back as a copy and the loop was
short. `check_song` and `review_arrangement` find those too, in music (not
in cues, sound effects or a song at speed 1), and each finding comes with
the call that fixes it, to send as it is: in the text after "Fix:", in the
JSON as `fix`.

- **A melody in one-bar pieces** (`phrases`): it stops at 70% or more of
  the bar lines inside its playing (nothing sounding over the beat before
  the line; 7 lines at the least, and 2 notes a bar or more, so sparse
  bells are left alone). Fix: `read_notes` with `format` `steps` over its
  bars, then `write_notes` with that line, a `-` in place of each `.` at
  the ends of the bars it names, so the note before the line holds into
  the phrase's second bar and the rest stays at the phrase's end.
- **A thin bass** (`bass`): the bass silent for a beat or more half the
  time or more, in short notes, over 8 bars or more, unless another low
  part carries the low end. Fix: `add_chords_and_bass` with `chord_channel`
  none and an `octaves` bass (`roots`, held, on a calm track) on the chords
  the parts play there: their roots alone, so it can't rub against the
  melody, as a walking bass's approach notes can.
- **A doubled bass** (`bass`): another channel on the bass's roots now and
  then, sounding in under a quarter of its bars: a thump, not a part. Fix:
  `write_notes` with no notes on that channel there, or give it a part of
  its own.
- **A sound its channel can't play** (`sound`): on the triangle, a sound
  whose envelope peaks at 10 or under (the triangle plays every level but
  0 at full, so a soft sound is a hard blip); on the VRC6's sawtooth, a
  pulse sound whose duty sequence plays odd values (each adds 32 to its
  volume: it overdrives). Fix: `set_instrument` with the `bass` preset on
  those notes.
- **Faders doing the mix's work** (`faders`): the melody's fader 4 dB down
  or more, or a part's 9 dB or more over the melody's, while the drums play
  louder than the style puts them or a part plays about as loud as the
  melody. Fix: `mix` with the style and `faders` `reset`, which sets the
  faders back to 0 dB first and the balance in the notes, as NSF files play
  it; the melody keeps the level its fader gave it, so the parts can reach
  their places under it.
- **A repeat that copies** (`repeat`): a named section that repeats an
  earlier one of its length note for note, or nearly (the melody's bars the
  same in 90% of them, every other part in 70%, at most one slow part
  added). Fix: `vary_part` embellishing the melody there; a counter-melody
  or a harmony on its bars, or busier drums, do too.
- **A short loop** (`short_loop`, with style `lively`): a stage that loops
  in under 55 s repeats too soon. Fix: `copy_bars` with the loop's next
  section (or its first bars) after its end, as many times as it takes,
  then `set_loop` to the copy's end. Battles, chases and calm tracks often
  loop sooner by design, so other styles leave it alone.
- **An intro that jumps into the main part** (`intro`; GitHub issue #1): the
  intro (a section named Intro at the start, else the bars before the loop,
  up to 16) against the bars after it. Its pulse jumps when the main part's
  notes start on three times as many rows a bar or more and its beat (the
  drums' hits and the bass's notes, 2 a bar or more) comes in from under a
  quarter of that, with nothing of it in the intro's last bar: a slow,
  sparse intro, then the whole band at once. Not on a calm track, which may
  start from stillness. Its loudness jumps when it plays more than 12 dB
  under the main part as heard (6-10 under is the target). Fix, for the
  pulse: `copy_bars` of the drums (without drums, the bass) of the loop's
  last bar, which leads back into A, into the intro's last bar: a fill brings
  the beat in before A. The loudness is advice alone, no call: keep the main
  part's pulse in the intro (fewer parts rather than longer notes) and make
  it 6-10 dB quieter, not slower; quieter still only makes the jump bigger.
  None of the 43 songs the user likes has it (44 of their 50 intros bring
  the beat in during their last bar; the others' main parts start notes on
  at most 1.8 times as many rows; the quietest intro plays 10.4 dB under).
  `what_next`'s intro builds intros that way: A's chords and bass in its
  rhythm, then, with drums, A's last bar of them in the intro's last bar.
- **A part too busy for the NES** (`too_busy`): the melody, or a line of
  its own (not an echo, a harmony in the tune's rhythm, chords, a steady
  arpeggio or the bass), plays many notes a bar with a rhythm it hasn't
  played before in almost every bar (its notes a bar times the share of
  such bars 6.3 or more, over 8 bars or more), so the ear never gets a
  pattern to hold on to. Fix: `vary_part` with `simplify` over its bars;
  better, a few short rhythms that come back. A local model's cave song
  scored 7.6 (8 notes a bar, 31 of 33 bars new); none of the 43 songs the
  user keeps reaches 5.2, and `review_arrangement` then leaves out its tips
  that would add to the part.
- **Chords that drone on** (`chord_drone`): held chords (a looping
  arpeggio instrument or `0xy` on half the notes or more, two notes a bar
  or fewer) that sound in 90% of the bars or more, three quarters of them
  on end without a rest of a beat, struck the same way in 90% of the bars
  they strike in, within 6 dB of the melody, on a track that isn't calm (a
  night song's pad holds by design). Fix: `vary_part` with `restrike` and a
  `rhythm`, section by section: `tresillo` in the A parts, `half` in the
  others, the intro and a break left held. A local model's lively stage
  struck its lush chords once a bar, every bar, for 40 bars at one volume,
  and the user found them annoying; the lush parts of the 43 songs the user
  keeps sound in every bar too, but play their most common bar rhythm in
  at most 59% of their bars (08 Verdant Run: whole notes in the intro, a
  tresillo at volumes 6, 4 and 5 in A, half notes in B). `what_next`'s
  lively recipe now strikes its lush chords that way.

`review_arrangement` weighs them into its score (a melody in pieces most)
and lists them among its tips, most first. Its breath check counts a rest
before a bar line only where it ends a phrase of two bars or more (a beat
short of it, for a pickup), so a melody that stops at every bar line no
longer breathes in every span; a rest inside a bar, or a note held two
beats, breathes as before. None of these findings comes up on the 56
finished game tracks they were measured against.

### Warnings as a part is written

`write_notes` says at once what a composer would want to know about the part
it just wrote, so a slip is caught before the song is built on it: the reply
ends with a line starting "Warning:" for each, with what to do and, where one
fits, the call that does it (after "Fix:"). It never refuses the part for
them. A local model's first song had a lone short note every other bar on
the triangle, on a pulse's soft sound, which nothing named until the song
was done.

- **Notes the channel doesn't play well**, in `check_song`'s words (the two
  share the check): a 2A03 pulse under A-0, the triangle over C-6, the FDS
  over G#7, where its pitch register tops out, an N163 wave over where the
  chip turns it grainy (A#5 with 4 channels and a 16-sample wave).
- **A bass too low to be heard as notes:** a note that sounds under 41 Hz,
  E-0 on the sawtooth (E-1 on the triangle, which plays an octave under the
  note written), a rumble on TV speakers and phones. FamiTracker's A-0 is
  55 Hz, where the soundtrack's own sawtooth basses go; none of its finished
  tracks goes under 41 Hz.
- **A melody an octave too high:** a line sitting mostly in octave 5 and
  reaching G-5 or higher, on a sound that holds (40% of its peak or more) at
  a melody's volume, where melodies sing in octave 3 and reach into octave 4
  at their high points (C-3 is middle C, A-3 440 Hz). Bells, music boxes and
  sparkles, which fade or play softly, sit up there by design, and so do
  echoes.
- **Notes outside the track's key**, when the key was chosen (`set_key`, or
  the piano roll's Key button): listed, with the question whether they're
  meant and the key's notes a semitone away. A borrowed chord's notes are
  meant: a note that another part also plays in that bar isn't asked about,
  nor one that makes a major or minor chord with two notes the other parts
  play there, one the key accounts for: borrowed from the major or minor key
  on its home note, or leading to another of its chords (a borrowed chord
  whose root only the bass plays, Db under F and Ab in Eb major; an A7's C#
  in F major; a slip a semitone off a chord's note makes none), nor
  a chromatic note of a beat or less stepping to or from its neighbour (an
  approach, a passing note). With a key found from the notes
  (major or minor, so a modal tune's own notes fall outside it), only a part
  with a third of its notes outside is asked about.
- **A bass nearly silent:** a lone short note now and then on the triangle
  or the sawtooth, four bars or more from its first note to its last (a bass
  that joins in an intro's last bars isn't, nor are bubbles over another
  part playing in most of those bars, every one above its notes). The fix is
  `add_chords_and_bass` with `chord_channel` none: a bass line there, on the
  chords the parts play.
- **A sound the channel doesn't play as it's meant:** on the triangle a
  sound meant to be soft (its envelope peaks at 10 or under), which it plays
  at full level until the envelope reaches 0, a hard blip (the fix:
  `set_instrument` with the `bass` preset); another chip's instrument, whose
  notes FamiTracker skips; on the sawtooth a VRC6 pulse sound that
  overdrives it.

The fixes move a whole part an octave with `transpose`; on the small set
(`/mcp/core`), which hasn't `transpose`, a line of steps comes back moved an
octave as a `write_notes` call. A part that became a clip gets the advice in
words, since the song doesn't have it yet. The noise and the DPCM get none
of these, and a sound effect only the first. On the soundtrack's 56
finished tracks and its cues, written back part by part, they ask about one
bar's notes outside a chosen key (a flat second coloring a harmonic minor)
and repeat `check_song`'s N163 range findings in one track; the local
model's draft gets its triangle warnings.

### Short replies

`review_arrangement`, `check_song` and `measure_mix` take `format`
`short`: what matters in a few lines, for a model with a small context.

- `review_arrangement`: the score, the parts, the numbers that matter (the
  length, the bass's notes a bar, the melody's breaths, the balance as
  heard) and the three tips that matter most, each with its call, or the
  tool its text names.
- `check_song`: the counts, then the three findings heard worst first, each
  with where it is and its call.
- `measure_mix`: the loop's loudness and peak, the intro, the sides, the
  four parts nearest the melody, and up to three things off the style's
  targets, each with its `set_mixer` call.

On the small set (`/mcp/core`) `review_arrangement`, `check_song` and `listen` answer
short unless a `format` is asked for (`text`, the whole reply; `json`,
`check_song`'s data). On the local
model's stage the review came to 1.1 KB from 4.9 and the checks to 1.0 from
3.9; on a finished 40-bar stage, 0.5 KB from 1.7 and 0.4 from 3.1.

### The checks in FamiTracker, and as data

FamiTracker runs `check_song` by itself too: **View > Problems** lists what
it finds in the selected track while you work by hand, and marks it in the
pattern editor and the piano roll (see `ROADMAP.md`, "Problems panel").

`check_song` with `format` `json` gives its findings as data, for programs
(the panel reads it): `track`, `track_name`, `from_bar`, `to_bar`, `key`
(and `key_from_notes` or `key_chosen`), `pattern_length`, `rows_per_beat`, `rows_per_bar`,
`counts` (the text's: `vanishing`, `click`, `clash`, `range`, `silent`,
`busy`, `key` and `held`, notes that vanish counted by the note, then the
arrangement's `phrases`, `bass`, `sound`, `faders`, `repeat` and
`short_loop`, `intro`, `too_busy` and `chord_drone`, see [What drafts miss](#what-drafts-miss), and `warble`), `style`
(`any`, `calm` or `lively`, as the check took the track), `unused_channels`
and `findings`, heard worst first as in the text. Each finding has:

- `kind` (as in `counts`) and `severity`: `high` for notes that vanish,
  clicks and clashes (`low` for a clash that's a detail), `medium` out of range, for a held lead, a melody in
  pieces, the bass, a sound its channel can't play, a part too busy
  for the NES and chords that drone on, `low` for the rest;
- `channel` (numbered from 1, as the tools take them; `null` for a silent
  bar) and `channel_name`;
- where it starts: `frame` and `row` (from 0, as the pattern editor shows
  them), `bar` and `beat` (from 1), and `position` ("bar 3 beat 2.5"), and
  `rows`, how long it lasts;
- `notes`: the notes it's about, each with `channel`, `frame`, `row`,
  `bar`, `beat`, `position`, `note` ("C#4", as written) and `rows`; a sound
  that vanishes on a channel is one finding with all its notes;
- `summary` (a few words) and `message` (the line the text gives);
- by kind: `other_channel`, `other_channel_name` and `pitches` (as they
  sound) for a clash, and `detail` (`passing` or `color`) for one that's a
  detail; `clicks`, `boundary` and `nudge` for a click;
  `instrument`, `instrument_name`, `heard_frames` and `held_instrument`
  for notes that vanish; `reason` out of range; `note_count` for a busy
  bar; `instrument`, `instrument_name`, `attack_frames`, `hold` and
  `note_count` for a held lead (on a calm track, see
  [Calm and lively tracks](#calm-and-lively-tracks));
- for the arrangement's: `bar_lines`, `stops`, `phrase_ends`, `spans` and
  `hold_bar_ends` for a melody in pieces; `problem` (`thin` or `doubled`),
  `sounding` and `silent` or `bass_channel` and `doubled` for the bass;
  `instrument`, `instrument_name`, `envelope_peak` and `problem` (`soft` or
  `overdrives`) for a sound; `faders`, `melody_fader`, `drums` and
  `nearest` for the faders; `section`, `repeats`, `melody_alike` and
  `parts_alike` for a repeat; `loop_seconds`, `loop_from_bar` and
  `loop_to_bar` for a short loop; `intro_from_bar`, `intro_to_bar`,
  `main_from_bar`, `main_to_bar`, `intro_starts_a_bar`, `main_starts_a_bar`,
  `intro_beat_a_bar`, `main_beat_a_bar`, `under_db`, `pulse_jumps` and
  `too_quiet` for an intro;
- `fix`, for a click that can be nudged, a clash `polish` can clear and
  every finding of the arrangement (but an intro's loudness alone, which is
  advice): the call that does it, `tool` (`polish`
  for a click: the touch `clicks` on the note's channel and bar; for a
  clash: the touch `clashes` on its two channels and bars), `arguments` and `does`, in
  words, and `then`, the calls to make after it, each the same way (a
  melody's `write_notes` after its `read_notes`). A call alone is one
  Ctrl+Z, and the problems panel's **Fix** button makes it (**Nudge** for a
  click); a fix with calls after it is Claude's to make, and **Explain**
  gives its calls.

## The agent log: why, and notes back

**View > Agent log** shows what an agent working on the song did and why, in
its own words, newest at the bottom: each line has its time, a colored tag
with its category's name in it (Idea, Plan, Research, Fix, Check, Note,
Error) and the text. The agent writes them with `write_log` as it goes:

```
write_log {"text": "Chose D dorian for the cave: dark but not sad.", "category": "plan"}
```

FamiTracker adds a quieter grey line of its own for every change a tool
makes, labelled as Edit > Undo labels it (`write_notes: 12 notes on 2A03
Pulse 1, bars 1-4`), also for what undo doesn't take back (a clip, a sound
added, a file saved), whether Claude Code, the Ask panel or one of
FamiTracker's own panels asked for it, and a red Error line for a call that
failed (any of an agent's, and those of FamiTracker's windows that would have
changed something). The toggles along the top show or hide each category,
the change lines (Changes) and your notes (You); FamiTracker remembers them.
**Clear** empties the song's log after asking; a right-click copies the lines
shown or docks the panel along the bottom or on the right.

Under the list, **Note for the agent** takes a note to the agents: Enter
sends it (Shift+Enter starts a new line). It shows in the log as yours,
"Not read by an agent yet" until an agent calls `read_user_notes`, which
gives it the notes it hasn't read with their times and marks them read. While
a note waits, the replies to a connected agent's other calls end by saying
so, so it doesn't have to ask after every step; the instructions tell it to
call `read_user_notes` between steps anyway.

The log is the open song's. FamiTracker keeps it while it runs and in a
text file next to the song, `<song> agent log.md` (`Town agent log.md` for
`Town.ftm`), one line an entry:

```
2026-10-30 22:31  [Fix]  The bass walks now; it sat on roots for 8 bars.
2026-10-30 22:32  [Change]  write_notes: 32 notes on 2A03 Triangle, bars 1-8
2026-10-30 22:35  [You, unread]  Keep the hook, it's the best part.
```

It's written as the log changes once the song has a file, and read back when
the song opens again. A song not saved yet keeps its log in memory and
writes it next to the song when it's first saved. The `.ftm` file itself
doesn't change.

## Backups: going back in one step

An agent often makes a batch of changes at once, where you'd work in small
steps, and undoing a batch that came out badly one Ctrl+Z at a time is
cumbersome (GitHub issue #4). A backup keeps the song as it is at a moment,
with a label saying why:

```
create_backup {"label": "before messing with the intro"}
list_backups {}
restore_backup {"backup": "intro"}
```

- `create_backup` (`label` required) saves a copy of the song as it is now,
  its changes not saved yet too, into a **Backups** folder beside the song's
  file, named with the time and the label (`Town 2026-10-31 14-05 before
  messing with the intro.ftm`), numbered `(2)` when two would share a name.
  The song itself doesn't change: not its file, its changes to save or its
  Ctrl+Z history. The labels are kept exactly in a small index beside them,
  `Town backups.txt` (a line a backup: number, time, file, label). A song
  without a file has no folder yet: save it first.
- `list_backups` lists them, the newest first: number, time, label
  (`format` `json` for programs). Numbers count up and are never reused. A
  backup file of the song the index doesn't name (copied in by hand, or the
  index lost) is listed too, its time and label read from its name.
- `restore_backup` (`backup`: a number, or a label or words of one; the
  newest of a label) first keeps a backup of the song as it is, labelled
  "before restoring ..." (so restoring loses nothing and can be taken back
  the same way), then opens the backup in the song's place. The song keeps
  its own file: saving writes the restored song there and never changes the
  backup, and until then it has changes to save. Opening a file clears
  FamiTracker's Ctrl+Z history, so undo starts afresh. It refuses while a
  dialog is open in FamiTracker, and a backup FamiTracker can't read before
  anything changes.

In FamiTracker, **File > Backups...** lists the open song's backups (time
and label), with **Create** (type why first), **Restore** (or a
double-click; it asks first) and **Open folder**. It runs the same three
tools, so a backup an agent kept shows there and one you keep is the agent's
to restore. Every backup and restore is a line in the agent log. All three
are in the small set; its instructions tell a local model to keep a backup
before a batch of changes.

## What Claude can do (the tools)

| Tool | What it does |
|---|---|
| `what_next` | The one next step for the song, its call ready to send, and why; what's done and what's left. See [One step at a time](#one-step-at-a-time-what_next) |
| `get_song` | Title, tempo in BPM (at speed 1, a sound effect's pace, the frames a row lasts, "1 frame a row", as the status bar shows it and the other replies that give a tempo say it), bars, the named sections and their bars, each channel and what it plays, instruments, clips, the track's key (chosen for it, or found from the notes), the chords that fit the melody bar by bar and the chords the parts play, whether the soundtrack has notes, where each part plays between the speakers (see [Stereo](#stereo)), and the channels whose fader, pan, Soft or Space in FamiTracker's mixer is off its default, with the space they send to (see [The mixer](#the-mixer)) |
| `read_notes` | One channel's notes: bar, beat, note, length in beats, instrument, volume, effects, and where a note plays when it isn't in the middle ("pan left (800)"). Up to 64 bars a call, or the whole bars of up to 600 notes (a ripple of sixteenths reads in two calls); when it reads fewer bars than asked, its first line says which and where the rest starts, so a part isn't rewritten from half of it. With `format` `steps`, the part as a line of steps, a bar to a line, as `write_notes` takes a melody (see [Melodies in steps](#melodies-in-steps)) |
| `write_notes` | Writes a part on a channel, replacing its notes in those bars (drums by name on the noise channel: kick, snare, hihat; on the DPCM channel the sampled kick, snare, low tom and high tom, each playing out with no halt after it, since 0.4.6 resets the DPCM level as it cuts a sample). A note before a rest ends with a release, or on the triangle, which can't fade, with a halt. A note can carry effects on its first row, later in it (`later_effects`: a fade into its tail, a slide) and where it ends (`end_effects`: on its release or halt, or on the next note when that starts right there). Speed changes, jumps, skips and halts (`Fxx`, `Bxx`, `Dxx`, `Cxx`) stay, so the song still loops the same way. A note's `pan` places it between the speakers (left, right, center, "half left", "30% right", a number from -1 to 1, or the effect, 840); the notes after it keep that place until one sets another, and notes without one play where the channel did there (see [Stereo](#stereo)). A detune (`Pxx`) stays the same way: notes written over bars an echo detunes (`P81`) play as detuned as the ones they replace, and so does the volume: notes that give none play as loud as the notes they replace (a soft part rewritten stays soft; in bars that had no notes the part starts at full volume). It replaces the bars its notes cover: whole bars its notes leave empty between them keep the notes they have (an intro and a break written in one call leave the drums between alone), and the reply names them; `from_bar` and `to_bar` together replace every bar from one to the other. A note that names no instrument (and no `instrument` for the part) plays the one the channel plays at its place, so a part that changes sounds keeps them, and the reply says which it used ("`"Lead" (00)` in bars 1-4, `"Bell" (03)` in bars 5-8"). On the noise and DPCM channels `instrument` can name a sound for each kind of hit (`{"hihat": "Hat", "open hihat": "Open hat", "kick": 3}`; kinds it doesn't name play their own); one instrument for a whole noise line plays every hit but the open hi-hats, which keep the open hat's sound (only its length makes a hi-hat open) unless that instrument rings as long, and the reply says so. A note that runs on past the song's end ends with it rather than adding a frame (use `insert_bars` first for more bars); notes that start past the end grow the song, and the reply says how long it is now. `notes` can also be a melody in steps, as text: `"D4 G4 A4 B4 - A4 G4 - | B4 - - - . . D5 -"` (see [Melodies in steps](#melodies-in-steps)). The reply warns at once of notes the channel doesn't play well, a bass too low to be heard or a melody an octave high, notes outside the key, a bass nearly silent and a sound the channel doesn't play as it's meant, each with what to do (see [Warnings as a part is written](#warnings-as-a-part-is-written)) |
| `place_clip` | Places a clip at a bar, as a copy or linked (direct changes only) |
| `add_instrument` | Adds a sound from a preset for a 2A03, VRC6 or 5B channel, one of the VRC7's or the 5B's own sounds, an N163 wave sound or an FDS sound (see [FDS sounds](#fds-sounds)) |
| `design_instrument` | Makes a new instrument from a description: attack, decay, sustain and release, the pulse width and how it moves, a vibrato that comes in after a moment, a blip, scoop, fall, flick or thump on the attack, a chord arpeggio, the calm leads (a music box, a breath, a falling strike), and the classic NES articulations (an attack that blooms, a duty click, a decay in steps, held notes struck again when asked with `restrike_beats` or words like "restrikes" or "struck again", which the reply then says; the bloom lead itself blooms and settles, an echo after the note); or a drum, darker or brighter, shorter or longer, or a swell on the noise channel (waves, wind); on the FDS channel a wave by word and what its modulator does (see [FDS sounds](#fds-sounds)); on the VRC6 Plus's soft layer a wave sound's waves; on the 5B its noise at a pitch. `words` takes it as you'd say it ("a plain 50% square that holds at full volume"). Its reply gives the volume a frame at a time and what that means at the song's tempo, and says when a sound is gone so fast that a tune's notes on it would sound missing. See [Sounds in words](#sounds-in-words) |
| `set_instrument` | Gives a channel's notes another instrument or preset |
| `remove_unused_instruments` | Lists the instruments no note plays in any track or clip (a template's starter sounds, sounds tried and replaced) and, with `confirm` once you agree, removes them and the sequences only they used, as Edit > Clean up > Remove unused instruments does (which now also keeps the instruments clips play). FamiTracker can't undo it; `keep` names any to keep |
| `transpose` | Moves notes by semitones, or into another key or mode (C major to C minor, lydian, dorian...); arpeggiated chords follow. Into another mode a dominant's ninth stays natural (E add9's F# in A major stays F# in A minor, where the scale's F would be a flat nine against the chord), and the reply says where |
| `set_tempo` | Sets the speed and tempo, from a BPM or as FamiTracker's own numbers: the track's, or from a bar on (a section's own tempo, a ritardando or accelerando over some bars) with speed and tempo effects there (direct changes only; undoable). See [Speed and tempo](#speed-and-tempo) |
| `set_meter` | Sets the meter (3/4, 6/8, 2/4, 12/8...) as FamiTracker's row highlights, which every tool counts bars and beats by; a track without notes also gets patterns of whole bars (direct changes only; undoable). See [Meter](#meter) |
| `set_swing` | Swings the rhythm: the speed alternates every half beat (or quarter beat), longer then shorter, averaging the old speed so the tempo stays; light (a lilt of about 57%, never over 61%), medium, hard or a percentage, as the speed's ticks allow; none takes it off; `by` delays swings only the channels named, with Gxx. A swing just before the bars counts as its middle speed (`F09` `F05` is speed 7), so bars after one, or put in after one, swing around the song's speed (direct changes only; undoable) |
| `mix` | Balances the parts, keeping each one's dynamics (`faders` `reset` first sets FamiTracker's mixer's faders back to 0 dB, pans, Soft and Space kept, so the notes carry the balance as NSF files play it; calm keeps the melody as loud as its fader played it; one Ctrl+Z undoes both): style soft (the default) mixes like atmospheric NES tracks, the melody's loudest at 9, an echo at 4, chords at 6, a second line at 7, drums at 10, and the DPCM level at Z30 so the triangle and the noise soften too; full puts them near full volume; or levels per channel (each one's loudest volume as written) and the DPCM level (dac) alone; over some bars, only they change: levels that step by section keep their steps, and the level after the bars is set back where they'd end on another (direct changes only; undoable). style calm (night and ambient tracks) and lively (stages) mix as heard: calm keeps the melody's level with the chords 2.5 dB under it; lively mixes a stage's band, the melody's loudest at 9 (its accents and steps kept), the bass about level with it, the chords together 3.5 dB under it and the band louder than it; the reply gives the balance reached; see [Calm and lively tracks](#calm-and-lively-tracks). A channel that echoes the melody in some frames and plays a line of its own in others is mixed as each, there. A part's loudest counts its notes as they play: an instrument whose envelope peaks at 10 plays 10 at volume 15, and a volume slide that swells it counts as high as it goes. Volume slides (`Axy`) move with the volumes, and a swell too slow to scale stops where it would climb past the part's level, the volume column following it a step at a time. With `width` (mono, narrow, wide, full) it sets the stereo picture: the melody, the bass and the drums in the middle, an echo on the other side, chords and second lines out to the sides. See [Mixing](#mixing) and [Stereo](#stereo) |
| `set_pan` | Places a channel's notes in some bars between the speakers: left, right, center, a place between, `alternate` (note by note left and right, by `amount`) or `spread` (from left to right across the bars); the note after them plays where it did. It warns when the melody, the bass or the drums go far to one side. See [Stereo](#stereo) (direct changes only, or as a clip; undoable) |
| `add_chords_and_bass` | Chords fitted to the melody, or a progression you give by name or degree, played in a style under the melody's lowest note and over the bass, with a bass line in a style. See [Arrangements with depth](#arrangements-with-depth) |
| `make_groove` | A backing to start a track from, for a mood (town, overworld, battle, boss, dungeon, sad, calm, chase, upbeat, shop, underwater, castle, desert, nostalgic): chords, bass and drums, each played as that mood usually is; under a melody written first, the chords go below it and keep their added tones off its notes. Its key becomes the track's key in the same change (as Start from a groove makes it), unless the track has one chosen or notes in another key already, so a melody grown on it after goes by that key |
| `add_drums` | A drum part on the noise channel: a groove, small changes every other bar, a fill where each phrase ends and a crash after it. Phrases count from bar 1, or from where the loop starts when an intro plays once before it (after a 4-bar intro, a fill every 8 bars ends bars 12, 20 and 28). The `lively` groove is for a gentle place, not too upbeat (a stage's beat is `basic` with `kit` samples): soft closed hi-hats on the eighths (4-6, a sixteenth pair now and then, more when busy), an open hi-hat every other bar (7), a soft kick on 1 and the "and" of 2 or 3 (10 and 8), no snare, small fills (a hi-hat roll or soft toms) and a soft splash after; with `kit` samples the kick is sampled. `intensity` light thins the kick and the snare (the kick on 1 and 3, one soft backbeat a bar) and keeps the hi-hats |
| `add_harmony` | A second voice following the melody a third (or sixth, fifth, octave) away, on the chords' notes where the melody holds. It follows where each note lands: after `polish`, a scoop counts where it ends, and the voice comes in on its own note rather than copying the scoop's slide; only the effects its channel plays are copied. A note that would rub a semitone (or a semitone and octaves, as `check_song` hears a clash) against another part as they sound, such as a held maj7 pad's seventh, goes to the nearest chord tone that doesn't, or is left out (an octave below keeps doubling the melody). Its thirds and sixths are the key's own, a harmonic minor's too (a sixth under A in D harmonic minor is C#), and a melody note outside the key gets the interval's size that lands in it; the chords it follows are the other parts' as FamiTracker plays them (a `0xy` lasts until its `000`, wherever that is), never the voice's own old notes, so a harmony written again doesn't follow the last one. Over a chord from outside the key (a borrowed bVI or bVII, a secondary dominant) the voice takes the scale that chord sounds in: under Ebadd9 in G major G aeolian's notes, under F G mixolydian's, never an Eb7's Db or an F7's Eb. Of the chords the other parts play, a note outside the key that no part plays isn't the chord's (an E bass under C and E is heard as an E augmented, whose G# nobody plays) |
| `add_counter_melody` | A line of its own from the chords' thirds and sevenths, holding while the melody moves and moving while it holds, below or above it; over a chord from outside the key (a secondary dominant, a diminished seventh) its passing notes keep off the key's notes that rub against the chord's. It moves at least every bar, mostly by step, never holding one note for bars. `style` calm leaves the melody room to breathe: the line holds a note a bar, rests where the melody does and answers only in rests of two beats or more, briefly (under a melody with no such rest the reply says there was no room to answer); `rests` (fill, the default, answer or leave) chooses that directly |
| `grow_melody` | Grows a hook (4-7 notes in steps, a bar or two) into a melody over the song's chords, in two or three versions to choose from, each a line of steps with a sentence on its shape: phrases of 2 and 4 bars (the hook, an answer, the hook moved up or down, a run on its opening, a climb to one high point a section, a close), chord tones on the strong beats and steps between them, a breath where each phrase ends, notes tied over bar lines inside a phrase, in the key and the channel's register. `pick` writes one as `write_notes` writes a line, `variant` gives others (the same ones each time), `kind` calm, lively or action shapes them. See [Growing a melody from a hook](#growing-a-melody-from-a-hook) |
| `polish` | Accents, a vibrato that comes in on the melody's long notes, gaps between repeated triangle notes, a nudge for notes whose vibrato clicks on the 2A03's pulses; when asked, `clashes`: an accompaniment's note a semitone from another part moves onto the chord the others play (see [Clashes](#clashes)); style calm leaves out accents and puts the shallowest vibrato (`451`) about half a second into long notes; style lively keeps the accents. Polishing again changes nothing it did: a part accented already keeps its volumes. `staccato` cuts the bass short only where the channel plays the bass (not a sawtooth counter-melody over the triangle's) and not in bars the melody rests in, where a break's long notes hold it up |
| `copy_bars` | Repeats bars elsewhere (A A B A), moved up or down if asked; the copy starts as the original played. The song's own jumps and ends stay where they were, and the reply names copies past the loop's end, which don't play until `set_loop` moves it. A whole section copied becomes its repeat, numbered the way the song numbers it: `A 2`, or `A3` beside an `A2`, never a name that reads as another's |
| `insert_bars` | Puts empty bars in at a bar, in every channel: everything after moves later, and the loop's jump back (`Bxx`) and the song's end move with it, so it loops as before (bars put in right after the end play too). Whole frames go into FamiTracker's frame list, as Insert frame does, so the frames after keep their patterns and linked clips; a bar or two inside a frame moves the rows after instead. Before the loop's start, inside a frame, only whole frames go in, since FamiTracker jumps back only to a frame's start. Bars put in after a swing swing as the bars before them do (direct changes only; undoable) |
| `remove_bars` | Takes bars out the same way: what comes after closes the gap, and the loop and the song's end move with it (direct changes only; undoable) |
| `song_shape` | Lays out a kind of song's shape: its named sections, each with its bars and what it's for, in the frames (an empty song gets the frames, the sections and its loop; a song with notes keeps them all, and gets the bars a longer shape needs only with `add_bars`). Without `shape` it lists the shapes. See [Song shapes](#song-shapes) (direct changes only; undoable) |
| `name_section` | Names bars as one of the song's sections (Intro, A, B, Bridge, Outro), renames a section or takes its name off. A section is whole frames, so bars inside a frame widen to it, and the reply says so. See [Sections](#sections) (direct changes only; undoable) |
| `set_key` | Chooses the track's key ("D dorian", "A minor pentatonic", or "auto" to find it from the notes again), as the piano roll's Key button does; it moves no notes. See [The key](#the-key) (direct changes only; undoable) |
| `set_mixer` | Sets channels' faders (`level` in dB, -24 to +6, or `change` to add), starting pans (`pan`), `soft` and `space` (0-100) in FamiTracker's mixer, and the space they send to (`room`, `echo` in beats, `repeats`), or puts them back (`reset`); `write_into_notes` writes the faders and pans into the notes for NSF files, in every track, and sets them back. See [The mixer](#the-mixer) (direct changes only; each call one undoable change) |
| `vary_part` | Changes a repeat a little: passing notes, only the notes on the beats, syncopation, an octave up or down, an ending on the tonic, held notes struck again on a rhythm (`restrike` with `rhythm`, default `tresillo`; softer off beat 1 as chord parts strike, so tresillo plays 6, 4 and 5 at volume 6). `ending` lands on the tonic, or over a chord without it (a turnaround's A7 in D major) on the chord's tone nearest the note. `embellish` works on a lively melody of short notes too: a passing note between notes a third apart, a neighbour between repeated ones, a note from beside into a long note (or a turn before it), at most two a bar, each taking a note's last sixteenth (the second half of a note of a beat or more); an ornament a semitone (or a semitone and octaves) from what another part sounds there is left out, as `check_song` would hear a clash |
| `review_arrangement` | How full the arrangement sounds, scored out of 10, and what would help most: missing layers, thin bars, plain or looping chords, a bass on roots, drums without fills, a melody without rests or one that hardly pauses (a rest of a beat or more, or a note held two beats, as it sounds, in under 45% of the two-bar spans it plays in, 75% on a calm track: a held note counts in the span it starts in, and a break without the melody isn't counted; the bars that run on named, and a busy part taken for the melody left out), melody notes on a sound that's gone within a few frames (they sound missing), sections pasted unchanged, flat dynamics, the stereo picture (an echo on its melody's side, a bass or drums off the middle), a melody buried under another part (counted as each chip plays, each note's instrument envelope in), parts crowding the melody's register while it sounds (a part doubling it, or trading notes with it in a hocket, doesn't), a song that keeps one loudness throughout (estimated bar by bar, each note as its instrument's envelope plays it frame by frame, so plucks die away and pads swell, the drums by how many hits they play and the DPCM level softening the triangle and the noise). The mixer's starting pans count, and an intro more than 10 dB under the rest is tipped (in a game it reads as silence). With style calm (or a track named calm, a night version) it isn't asked for drums, fills, accents, more notes or shorter ones; it hears the balance as the stems would and tips a melody more than 4 dB over the chords, and warns about a lead whose sound holds a high, steady level; lively hears a stage's balance (3-4 dB). A cue or a jingle (a track that halts within half a minute) is judged as one: the tips for looping stages (the length, variety, fills and a sampled kit, a bass that moves, chord colors, a beat, a lead's steady level) are left out, those about notes that vanish, clashing parts and the balance stay; a sound effect gets a short check of its own, without a score. It also finds what drafts miss, each with the call that fixes it (see [What drafts miss](#what-drafts-miss)), and a rest before a bar line breathes only where it ends a phrase of two bars or more. `format` short gives the score, the numbers that matter and the three tips that matter most (see [Short replies](#short-replies)). See [Measuring the mix](#measuring-the-mix) for how it tells an echo, a sparkle and a soft bass |
| `soundtrack_notes` | Reads or adds to a text file next to the song that keeps your taste and the decisions about your game's music between sessions |
| `study_nsf` | Studies tracks of an NSF file you have (game music) and keeps what makes each one work in your reference library. See [References: learning from NES soundtracks](#references-learning-from-nes-soundtracks) |
| `study_module` | Studies a tracker module you have (MOD, S3M, XM or IT) from its patterns, a MIDI file from its notes, or a FamiTracker module (.ftm) track by track, and keeps it in your reference library too, so new NES music can borrow its style. See [Modules as references](#modules-as-references) |
| `references` | Lists, shows and searches the reference library, and the built-in guide to the classic NES styles (Konami, Capcom, Nintendo, Square, Sunsoft, Technos) and to dark and melancholic music |
| `effects` | FamiTracker 0.4.6's effects (what each does, on which channels, how long it lasts, what it's good for) and the techniques made with them. See [Effects and techniques](#effects-and-techniques) |
| `explain` | A music, sound or tracker term in plain words, as FamiTracker's glossary (**Help > Glossary**) explains it to you: what it is, an everyday comparison, where it is in FamiTracker, what its Hear it example plays and the terms to see too; an effect as written (`447`) with what its numbers do. Without a term, the terms by category. See [The glossary](#the-glossary) |
| `write_log` | Writes a line into FamiTracker's agent log (**View > Agent log**): `text`, and a `category` (idea, plan, research, fix, check, note or error; note when none is given; any other is refused with the list). FamiTracker stamps the time; the song and its undo history stay as they are. See [The agent log](#the-agent-log-why-and-notes-back) |
| `read_user_notes` | The notes you wrote to the agents in the agent log's note box since they last read them, each with its time, marked read; "No new notes from the user." when there are none. `all` true gives every note |
| `check_exercise` | One of FamiTracker's exercises for newcomers (**Help > Exercises**) checked on the selected track as its Check button checks it: what's right already, the one thing to change next in plain words, the bars that show it, and once it passes, what it taught and the exercise after it (`format` `json`: the same as data). Without an exercise, the list. See [Exercises](#exercises) |
| `reference_sound` | Adds one of a reference's sounds to the song as an instrument, with the original's exact envelopes, and says when it's gone so fast that a tune's notes on it would sound missing. It also takes a track of the twelve soundtracks FamiTracker carries by its id (`castlevania-02`), with or without the library. An FDS game's sound plays on the FDS channel with its own wave and modulator (see [FDS sounds](#fds-sounds)); on an N163 channel or the VRC6 Plus's soft layer any melodic sound becomes a wave sound, an N163 game's with its own waves (see [VRC6 Plus](#vrc6-plus)) |
| `find_sound` | Searches every sound the song can get in one list, as **Instrument > Sound browser** shows them: the ready-made sounds for its chips (the presets and FamiTracker's own sound library, some 230 more sounds of every kind for every chip's channels), the studied sounds of the twelve soundtracks FamiTracker carries (see [Sounds from twelve soundtracks](#sounds-from-twelve-soundtracks)), each sound of your reference library's studies and the song's own instruments, by words ("warm bell", "pluck", a game; when no sound has every word, the ones with some), kind (lead, bass, pad, pluck, bell, keys, drums, effect) and source. Studied sounds of the same name, of one study or two, are numbered ("Green Planet lead 2"), so each can be added. Each comes with where it's from, the channel it plays on, its words, the call that adds it (`add_instrument` or `reference_sound`) and the warnings: notes gone within four frames, a vibrato that clicks on the 2A03's pulses. With `other_chips`, the ready-made sounds of the chips the song doesn't have come too, after its own, each with the `set_expansion` call that adds its chip first (every note stays). With a channel, every sound that plays there |
| `import_nsf` | Opens tracks of an NSF file as a new module (the same as File > Import NSF), to look at or play them |
| `send_file` | For a client on another computer: puts a file (an NSF, a module, a MIDI file, a WAV recording) into the Inbox folder next to FamiTracker.exe and gives its path for the tools that read files. See [Files between the computers](#files-between-the-computers) |
| `add_sample_drums` | Adds a kit of sampled drums for the DPCM channel, made for FamiTracker (a kick, a snare and two toms); `write_notes` then plays them there by name |
| `add_echo` | Copies a part onto a free channel, later and quieter (it stops at the song's end, so the loop stays where it is); on another chip it plays the part's preset there, written where it sounds at the part's pitch (an octave up on the VRC7 and the 5B), and past the bars asked for it rings on only until the target's own next note. Each echo note plays about 8 dB under its original as both sounds play (each chip's level and both instruments' envelopes counted; the mixer's faders stay the mixer's, so the volume column doesn't make up for one), and it copies only the effects its channel plays (an FDS modulator's `Hxx`-`Jxx` isn't put on a pulse). An echo note a semitone from a note another part sounds with it, or a semitone and octaves (a minor ninth, an F-4 over a pad's E-2: what `check_song` finds a clash, by the same rule), is left out or cut short; a major seventh (over the bass, or under the melody) is a chord's color and stays. With `pan` "opposite" each echo note plays on the other side of its original (or give a place: left, right, half right...) |
| `add_vibrato` | Vibrato on the longer notes, starting just after each note begins: faint (`4x1`, the shallowest), gentle, medium or strong. A depth by word swings the notes as far as it swings a 2A03 pulse's, so on the FDS, the N163 and the VRC7, whose steps are finer or coarser, the effect's depth is another (the reply says the cents). `modulator` `off` on the FDS turns its modulator off where it plays (`H00` on each note, the `Hxx` that bring it in taken out) and brings the vibrato in where the modulator came in |
| `change_mood` | Several changes for a mood at once: key or mode (a dominant's ninth kept natural, as `transpose` keeps it), parts up or down, new sounds, vibrato, an echo and the tempo |
| `check_song` | Notes outside the key (a slide that takes a note outside it too; not a borrowed chord's or a secondary dominant's, which it hears as `write_notes`' warnings do, while under a quarter of the notes fall outside the key), clashing channels (a semitone apart, or a semitone and octaves), notes out of a channel's range, busy and silent bars, notes that click on the 2A03's pulses, notes of a line that vanish between its held notes (they sound missing), unused channels. Notes count as they sound: a scoop where it lands, a slide later in a note where it goes, the triangle, the VRC7 and the 5B an octave under their written notes, the FDS two, a chord's arpeggio (an instrument's or 0xy) with all its notes, and not a fall as a note ends. N163 notes too high for the channel count are out of range: the chip plays its channels in turn, so with 4 of them a 16-sample wave skips samples (turns grainy) above A#5; so are FDS notes above G#7, where its pitch register tops out, and below A-1, which it plays under 28 Hz. With style calm (or a track named calm) it also finds a lead whose sound holds a high, steady level from attack to release (`held`); lively gives that as a tip. In the arrangement it finds what drafts miss (a melody in one-bar pieces, a thin or doubled bass, a sound its channel can't play, faders doing the mix's work, a repeat that copies, a lively stage's short loop, an intro whose pulse or loudness jumps into the main part), each with the call that fixes it (the intro's loudness as advice) (see [What drafts miss](#what-drafts-miss)). `format` short gives the counts and the three findings that matter most with their calls; `json` every finding as data (see [The checks in FamiTracker, and as data](#the-checks-in-famitracker-and-as-data)) |
| `name_song` | Sets the module's title, author and copyright (at most 31 characters each) and the selected track's name, which `export_for_game` names its file and GameMaker sound after (direct changes only; not undoable) |
| `make_version` | Another version of the selected track as a new track: a night version, a tense one for battles, an area's variant. The original stays as it was. A night version's lush chords keep their own sounds, which a pad would lose |
| `make_jingle` | A jingle as a new track: a short tune that plays once (level up, victory, item, new quest, discovery, rest, defeat, danger), in the song's key note and sounds (the music's, never a sound effect's) |
| `make_sound_effect` | A sound effect as a new track: a jump, a coin, a menu blip, wind, rain, birds and 28 more kinds, a sound described in words (built from layers), or a recording of your voice ("pew", "boing", "kssh"). Ambience loops, as long as asked (30 seconds of rain or wind). See [Sound effects](#sound-effects) |
| `select_track` | Switches to another track of the module, by number or name; the tools work on the selected one |
| `keep_version` | Keeps the selected track as it is now as a track of its own ("Town (version 1)", or named, like "Town (calm bridge)"), and stays on the selected track, so Claude can try something and you can compare or go back |
| `compare_versions` | Plays the same bars of two versions or more for you, one after the other (by default the newest kept version, then the track as it is now), and shows the track worked on again afterwards |
| `set_expansion` | Sets the expansion chips (any of VRC6, VRC7, FDS, MMC5, N163 and 5B, together, or none, or the VRC6 Plus), as Module > Module properties does. See [Expansion chips](#expansion-chips) and [VRC6 Plus](#vrc6-plus) |
| `check_loop` | How the song loops in a game: the intro that plays once, the part that repeats and how long each lasts, and the seam: notes hanging over it, effects, instruments, volumes, pans or tempo that differ the first time and on repeat, the chords and the melody's step across it |
| `fix_loop` | Writes what `check_loop` finds at the seam, so the repeats start as the first time does: a release where a note from the loop's end on a sound that fades on one (a pad's held chord) would hang over into a silence, so it fades there, else a halt (an echo's last note), and at the loop's start the instrument, volume, effects (a fade's volume slide, a vibrato), pan and speed and tempo the first time has. Only what changes nothing the first time (direct changes only; undoable) |
| `set_loop` | Sets where the loop starts (an intro before it) and ends, with a jump (`Bxx`), or with `stop` makes the song play once and stop (a halt, `Cxx`, after `to_bar` or at `stop_beat` in it); other jumps and halts go. The reply says so if the song still loops elsewhere, as when a skip (`Dxx`) ends the last frame early (direct changes only; undoable) |
| `export_for_game` | Saves tracks as looping OGG files next to the song, with the GameMaker code that plays them (the same as File > Export for games); each is brought up so its loudest peak is at -1 dBFS, since FamiTracker renders well under full scale (`normalize` false keeps its level). With `loudness` (LUFS, like -20) each track's loop is brought to that loudness instead, as EBU R128 measures it (`IntegratedLoudness` in `GameAudio.cpp`, over both channels; a sound shorter than its 400 ms blocks over its whole length, `SoundLoudness`), so a game's tracks sound equally loud; the peak still stops at -1 dBFS. The files are stereo: a song that doesn't pan has the same on both sides. FamiTracker's mixer (its faders, starting pans, Soft and Space, see [The mixer](#the-mixer)) plays in them; with Space on, the loop is the song's second time through, so the tail of its end is in the loop's start. A song never saved is saved first, for a client (as `save_song` saves it); over the network the reply links each file with a code of its own, which a browser opens with no header, and as an MCP resource, and `attach` puts the files in the reply. See [Files between the computers](#files-between-the-computers) |
| `finish_song` | The last steps of a song in one call, for its kind: `polish` with the kind's touches, `mix` in the kind's style (its faders back at 0 dB first where they do the mix's work), `check_loop` and `fix_loop` where the seam needs it, a `listen` to the song as it then sounds (kept, so `what_next` takes it as heard; the reply lists its high findings with their fixes), and `export_for_game` at the kind's loudness; one Ctrl+Z undoes the song's changes, and the reply is a line a step and what to listen for. See [The last steps in one call](#the-last-steps-in-one-call) (direct changes only) |
| `measure_mix` | Measures the selected track as `export_for_game` renders it, changing nothing: the loudness and peak of each section (or every 4 bars) and of the loop, the intro against the loop, the two sides, and each part alone (the melody, the chords together, each other channel) against the melody where both play, or against the whole when there's no melody (ambience); `style` calm or lively checks the soundtrack's targets. See [Measuring the mix](#measuring-the-mix) |
| `look_at_song` | A picture of the notes for Claude to look at: a piano roll of up to 32 bars, each channel in its own color, drums in a strip below, arpeggio chords as faint bars, where the song loops, rows that never play hatched, and an L or an R on notes panned to a side; the reply names the chords heard bar by bar |
| `play`, `stop` | Plays bars for you: over and over (`loop`) to listen closely, and with only some channels (`channels`) or without some (`mute`), to hear a part alone or the rest without it. Your own mutes come back when it stops (whole frames loop without a gap; other bars start again a moment after they end) |
| `undo` | Ctrl+Z, and says what it undid: the call that made the change ("write_notes: 12 notes on VRC6 Pulse 1, bars 1-4") or an edit made in FamiTracker. A call that fails leaves nothing of its own to undo (what it had changed by then is taken back, and Ctrl+Y doesn't bring it back), so undo after it undoes the change before it, and says so (direct changes only) |
| `save_song` | Saves the song, or a copy next to it to compare versions. A song never saved goes into the Songs folder next to FamiTracker.exe, in a folder of its own named `name` (else after its track), numbered when the name is taken; it then is that file. See [Files between the computers](#files-between-the-computers) |
| `new_song` | Starts a new song in place of the open one, as File > New does (the profile's template: its chip, tempo and sounds), with no dialog; `expansion` picks its chips as `set_expansion` takes them, and `name` is its title and its track's name. Asked over MCP, it's saved at once into the Songs folder under that name. Refuses, changing nothing, while the open song has changes that aren't saved. See [A new song, and back to an old one](#a-new-song-and-back-to-an-old-one) |
| `create_backup`, `list_backups`, `restore_backup` | A backup of the song as it is (changes not saved yet too) with a label, in a Backups folder beside its file; the backups listed, the newest first; one opened in the song's place by number or label, after a backup of the song as it is, the song keeping its own file. See [Backups: going back in one step](#backups-going-back-in-one-step) |
| `open_song` | Opens another song by name: one in the Songs folder or among FamiTracker's recent files (`list` names them, the newest first); from this computer, `path` opens any `.ftm` file. Refuses while the open song has changes that aren't saved |

Instrument presets: soft lead, bright lead, thin lead, pluck, bell, soft
chords, lush chords, warm pad, bass, thump bass (an octave blip into each
note, the punch a triangle bass can't get from volume), plucky bass, singing
lead (a vibrato that comes in by itself), strings, brass, organ, the calm
leads music box, breath and falling strike (see [Sounds in
words](#sounds-in-words)), kick, snare, hi-hat, open hi-hat, crash, tom.
Each is built for the 2A03 (also MMC5 channels) and the VRC6, and plays on
the Sunsoft 5B too. VRC7 channels play the chip's own sounds instead, and
the 5B has its own besides (see [Expansion chips](#expansion-chips)).

Notes are written the way FamiTracker shows them: C-4, C#4 (Db4 works too).
FamiTracker's octave numbers are one lower than in most music software: C-3
is middle C and A-3 is 440 Hz. Bars and beats count from 1, as the row
highlight shows them (the first highlight is a beat, the second a bar).
Claude works on the track that's selected.

### Melodies in steps

A melody is easiest to write as a line of steps: there are no beats to add
up, so a note can't end early by a slip of arithmetic. `write_notes` takes
`notes` as text, from `from_bar` (or from the bar number the text starts
with, `5:`):

    5: D4 G4 A4 B4 - A4 G4 -
    6: B4 - - - . . D5 -

Every token is one step of a grid: an eighth, unless `step` says
`sixteenth` or `quarter`. A pitch starts a note (`D4`, `C#4`, `Bb3`, or
FamiTracker's `D-4`), `-` holds the note before it one step more, across a
bar line too (a tie into the next bar), and `.` is a step of rest. A note
lasts until the next pitch or rest, so a phrase is legato unless a gap is
written, and it breathes where its 2 or 4 bars end, not at every bar line.
`|` or a new line ends a bar, and every bar holds the meter's steps: 8
eighths in 4/4, 6 in 3/4 and 6 in 6/8, where a beat is three eighths (an
eighth is a third of a beat wherever a beat has 3, 6, 9 or 12 rows, as
`set_meter` makes 6/8; half a beat otherwise). A line with a bar of more or
fewer steps is refused, every such bar named with its count, and nothing is
written. A pitch may carry its volume, `B4v12` (0-15), which the notes after
it keep; on the noise channel the drums go by name (`kick . hihat . snare .
open hihat .`), a name of two words one step (`open hihat`, `low tom`,
`snare drum`) and hits in a row each a step of their own (`hihat hihat`).
A line that rests before most of its bar lines gets a note
in the reply about phrases.

`read_notes` with `format` `steps` gives a part back the same way, a bar to
a line with its number, to change and write again. Notes that fall between
the eighths read in sixteenths (the reply says so, and the line goes back
with `step` `sixteenth`); a part with notes between the sixteenths too
reads as the usual list. What the line doesn't show is listed with it: the
instruments where they change, the effects, a note held into the first bar
(the line starts with `-`) and one ringing on past the last.

Written back, a note at the same step with the same pitch as the song's is
the song's note kept as it was: its instrument, its effects (those within
its length), the release or halt that ends it where it ends as before, and
the effects that were on as it started (a vibrato it inherited from the
note before), so a line read and written back unchanged gives the same
cells. A new or moved note gets the line's instrument (`instrument`, or
what the channel played with) and no effects. A `-` at the line's start
holds the note sounding into it, the last note of a line written before
too (a tie from one call to the next), and a kept last note that rang on
into the next bar goes on ringing, as before; a `.` at the line's end stops
it there. The list of notes works as before.

### Growing a melody from a hook

A melody over many bars is easiest to grow from its hook. Write the hook
in steps (4-7 notes, a bar or two) and `grow_melody` grows it over the
song's chords (the ones the other parts play, or `chords` you give) for 8
bars from `from_bar` (or from the bar number the hook's line starts with;
`to_bar` or a `section` for others, up to 32 bars), in three versions:

    grow_melody {"motif": "D4 G4 A4 B4 - A4 G4 -", "section": "A"}

Each version is a line of steps that `write_notes` takes as it is, its
bars numbered, with a sentence on its shape: "Question and answer: bars
5-8 the hook, an answer, the hook a step higher and a half close on F#4;
bars 9-12 the hook again, an answer, up to the high point E5 and a close
on A4. Phrases of 4 bars, the high point E5 in bar 11; it breathes in 4 of
4 two-bar spans." The forms are a question and its answer, the hook and
its sequence, and two-bar calls. In every version:

- the phrases are 2 and 4 bars long and breathe where they end (a rest of
  a beat or more), and a 4-bar phrase holds a note two beats in its
  middle, so `review_arrangement` hears the melody pause in nearly every
  two-bar span;
- the notes on the strong beats (beats 1 and 3) are the chord's, the ones
  between them move mostly by step, none sits a semitone from what the
  other parts play for half a beat (what `check_song` hears as a clash),
  and all are the key's own (in a minor key the raised seventh over its
  dominant);
- each section reaches one high point, a chord tone above the hook's
  highest note, and the line keeps around the hook's register;
- the hook comes back as written where the chords let it, and moved up or
  down the scale where they don't;
- long and short notes mix, and notes tie over bar lines inside a phrase.

`kind` shapes it: calm grows fewer, longer notes that move by step, in
phrases of 2 bars that each end with a rest; lively (the default) more
motion; action drive (eighths, repeated notes). Without `kind`, a track
named calm (a night version) grows calm, and one named for a battle, a
boss or a chase grows action. `variant` 2, 3... gives other versions, the
same ones every time. `pick` with a version's number (and the same
variant) writes it on the channel (`channel`, else the melody playing
there, else a free pulse) as `write_notes` writes a line, a clip unless
direct changes are allowed. Then shape it by hand: `read_notes` with
`format` `steps`, change a note or a rhythm, and write it back.

### Meter

`set_meter` sets the row highlights for a meter, so a waltz counts three
beats to a bar everywhere: in `write_notes`, the chords, the drums, the
review and the loop. 3/4 keeps the song's rows a beat (4: 12 rows a bar);
6/8, 9/8 and 12/8 count dotted quarters of three eighths (6 rows a beat, an
eighth every 2 rows: the eighths of beat 1 are beats 1, 1.333 and 1.667),
and 5/8 or 7/8 count eighths. A track without notes also gets patterns of
whole bars (4 to a frame, or `bars_per_frame`); a track with notes keeps its
patterns, so the meter is set before writing (a bar that doesn't divide the
patterns is refused). In 0.4.6 the highlights are the module's, so every
track counts in the same meter. After a change to a compound meter the BPM
counts dotted quarters: `set_tempo` with bpm picks the speed for them.

### Sections

A track's frames can carry names: Intro, A, B, Bridge, Outro. In
FamiTracker, select frames in the frame editor and right-click > Name
section. Each section is a coloured band beside its frames, where the
loop's start shows too (a double-click on a band renames its section), and
the piano roll's ruler names it at its first bar. Right-click also renames
a section, takes its name off, selects its frames, moves it up or down
past the section beside it (patterns and all), and duplicates it after
itself (the copy plays the same patterns). Each is one Ctrl+Z, and
inserting, removing, moving or duplicating frames keeps the sections with
their frames.

Claude names the sections as it builds the song's form (`name_section`),
and `get_song` lists them with their bars. The tools that take bars
(`read_notes`, `play`, `look_at_song`, `copy_bars`, `remove_bars`,
`set_loop`, `set_tempo`, `add_chords_and_bass`, `check_song`...) take a
section's name instead: `play` with `section` "Bridge". `insert_bars` can
name whole frames it puts in, `copy_bars` names a whole section's copy as
its repeat ("A 2", or "A3" beside an "A2"), `remove_bars` takes a removed section's name with it,
`look_at_song` labels the sections in its picture (apostrophes, commas and
the like in their names too), and `keep_version` and
`make_version` keep them. A section is whole frames, so bars moved by less
than a frame leave the sections where they were, and the reply says so.
The names are saved in the module in a block of their own (`BF SECTIONS`)
that other FamiTracker versions skip.

### Song shapes

A newcomer facing an empty frame list doesn't know how long a song is or
what its parts are for. A song shape lays them out at once: the named
sections a kind of song needs, each with its bars and a line on what it's
for. In FamiTracker it's Module > Song shape (also at the bottom of the
frame editor's right-click menu, and a right-click on the piano roll's
ruler): pick a shape, read its parts, and Lay it out. Claude does the same
with `song_shape`, and `what_next` starts an empty song with it.

| Shape | Its parts (bars) | For |
|---|---|---|
| `stage` | Intro 4, A 8, A' 8, B 8, Climax 8, Break 4 (40) | a lively stage: it bounces, builds to a climax, breathes and loops |
| `night` | Intro 4, A 8, A' 8, B 8, Break 4, A'' 8 (40) | a calm night or a place to rest |
| `town` | Intro 4, A 8, A' 8, B 8, A'' 8, Break 4 (40) | a town, a village or a field: a tune that loops a long time |
| `boss` | Intro 2, A 8, A' 8, Climax 6, Break 2 (26) | a boss fight or a chase |
| `small` | Intro 2, A 4, A' 4, B 4 (14) | a small place: a shop, a house, a menu |
| `jingle` | Phrase 2, Last chord 2 (4) | a jingle that plays once and stops |

They're the forms the recipes teach: an intro that plays once, the tune
(A), its repeat changed a little (A'), something different on other
chords (B) so A sounds fresh when it comes back, a high point (the climax,
or A'' at its fullest) and a break, a breath before the loop comes round (a
calm night's comes before its last A, as the soundtrack's night songs had
it).
`song_shape` without `shape` lists every shape with each part's line ("A:
The tune, the part people hum."; "B: Something different, on other
chords, so A sounds fresh when it comes back."; "Break: A breath before
the loop comes round: the tune and the drums rest.").

- **On a song without notes** it makes the frames the bars need, at the
  song's rows a bar: frames of the song's own length when every part is
  whole frames of it, else of the most whole bars every part is (frames of
  2 bars for a boss's 2-bar intro; 48 rows in 3/4, 96 in 6/8 at 24 rows a
  bar). It names the sections and writes the loop: the intro plays once,
  then the rest loops (a `Bxx` on the last row); a jingle stops at its end
  (`C00`). The song's own jumps and halts go; its speed stays.
- **On a song with notes** every note stays where it is. The sections go
  over the bars it has (a part of bars that aren't whole frames of the
  song's takes the frames nearest them, and the reply says so); a song
  shorter than the shape gets the empty bars the rest needs at its end only
  with `add_bars` (FamiTracker asks first), and its jump back moves to the
  new end so they play. A longer song keeps its last bars without a
  section. Its own sections give way to the shape's. FamiTracker's list
  marks the shapes that fit in the bars such a song has.
- Each is one Ctrl+Z, the frames' new length too.

Where a section is named, FamiTracker says what it's for: pointing at its
band in the frame editor shows its line as a tooltip, and at its name on
the piano roll's ruler in the piano roll's status line ("A' (bars 13-20):
The tune again, changed a little: a second line joins in and the drums get
busier."). The line is worked out from the section's name and the shape
the track's sections are (else the one the track's name says: "Boss",
"Night"), so nothing new is saved and older songs' names get lines too (Intro, A, A2,
A', B, Bridge, Verse, Chorus, Climax, Break, Outro). A part's repeats (A',
A'', A 2) have its color in the frame editor and on the ruler.

### The key

Each track has a key: the one chosen for it, with the Key button above
FamiTracker's piano roll or with `set_key`, or else the one its notes fit
(found as `get_song` finds it). The piano roll lights the key's notes, names
the chords the parts play in its spelling (the same chords `get_song` lists)
and, with Snap to key, lands the notes the user draws on the key's notes.
`get_song` says whether the key is chosen or found (and, for a chosen one,
what the notes alone fit), and every tool that takes a `key` uses the
track's when it isn't given one. A key moves no notes: `transpose` with
`to_key` (or `change_mood` for the whole song) moves them, and a chosen key
goes along when the whole song moves, in the same undoable change. Versions
(`keep_version`, `make_version`) keep it. A pentatonic key (a major or minor
key's five notes) keeps the piano roll to those notes; the tools use the key
itself. The choices are saved in the module in a block of their own
(`BF KEYS`) that other FamiTracker versions skip.

## Speed and tempo

FamiTracker counts time in ticks, 60 a second (50 for PAL). The **speed** is
how many ticks a row lasts, and the **tempo** stretches that: at tempo 150
(125 for PAL) a row lasts exactly the speed's ticks. Most FamiTracker songs
keep tempo 150 and change the speed. At 4 rows a beat:

| Speed | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|
| BPM at tempo 150 | 300 | 225 | 180 | 150 | 128.6 | 112.5 | 100 | 90 |

Ask for a BPM and Claude picks the speed and tempo. Every row then lasts the
same number of ticks when that's possible within 1% of the BPM, usually by
changing the speed at tempo 150. Other BPMs need another tempo, and then rows
take turns being a tick longer: 120 BPM at speed 6 and tempo 120 alternates
rows of 7 and 8 ticks. You can't hear it in most songs, but Claude says so
and names the even choices nearby. Ask for a speed or a tempo ("speed 5",
"tempo 150") and Claude sets exactly that. When the song changes its speed
with `Fxx` effects, a BPM changes the tempo only, since those effects would
set the speed back. Night and tense versions pick their tempo the same way,
unless the song has `Fxx` effects: then they scale the tempo, and the tempo
effects along with it.

**A tempo for some bars** comes from `set_tempo` with `from_bar` (and
`beat`): speed and tempo effects (`Fxx`) there set it, and after `to_bar` the
song plays at the tempo it had (without `to_bar` the new one holds to the
end). With `over_bars` it changes gradually, a step every beat: a ritardando
for an ending that slows down, or an accelerando. The steps keep one speed
and move the tempo, and the last one lands on even rows: from 112.5 to 56.25
BPM the tempo steps down at speed 8 and ends at speed 16, tempo 150. Speed
changes inside those bars (a swing) stay, and the tempo carries the change.
Where the song loops back into the change or past it, the loop's start gets
the speed and tempo it has the first time, so every repeat plays alike.
`change_mood` with a `bpm` or `tempo_percent` for some bars writes the same
effects within its one Ctrl+Z step; for the whole song it sets the track's
tempo as before. **A song that stops** (credits, an ending, a game over) gets
a halt (`Cxx`) from `set_loop` with `stop`: on the last row of `to_bar`, or
at `stop_beat` in it, and `export_for_game` then makes a file that plays once.
FamiTracker cuts what still sounds when the halt's row comes, so a last chord
rings out or fades before it.

**Swing** (`set_swing`) works the way FamiTracker 0.4.6 composers do it: the
speed changes every half beat, longer then shorter, so the rows themselves
swing and every channel's off-beat notes come late together, notes written
later too. The two speeds average the old one, so the tempo stays: at speed
6, `F08` and `F04` in turn is a triplet feel (67%) and `F07` and `F05` a light
one (58%). Light is a lilt of 56-58%, never over 61% (at speed 8, `F09` and
`F07`: 56%). The speed's ticks decide which swings fit, and Claude says which
when the one asked for doesn't (at speed 4 the gentlest is 63%, too much for
light); `unit` sixteenths swings quarter beats
instead, and `none` takes it off. The speed changes go on the channel that
has the song's speed effects, else the last one. A swing just before the
bars counts as its middle speed (`F09` and `F05` are speed 7, not 5), so
bars swung after one, and bars `insert_bars` puts in after one (which swing
as the bars before them do), swing around the song's speed. Change the speed with
`set_tempo` afterwards and the swing needs setting again; a BPM keeps the
speed and changes the tempo, which keeps the swing.

With `by` delays, only the channels named swing: each of their notes on
the second eighth (or sixteenth) of a pair starts a few frames late
(`Gxx`, worked out from each row's speed), the way FamiTracker composers
shuffle hi-hats or a busy arpeggio over a straight bass; `none` with the
same `unit` takes those delays off.

## Mixing

A NES song played with every pulse at full volume sounds harsh, and the
pulses bury the triangle, which has no volume of its own. Atmospheric NES
tracks keep the pulses well down instead: a lead around 4 to 8, its echo at
2 or 3, chords at 4 to 7, and the triangle carrying the low end. `mix` does
that in one undoable step. It works out what each channel plays (the
melody, an echo of it, chords, a second line, a bass, drums) and scales
each channel's volume column so its loudest note plays at that role's
level, keeping its accents and fades. style soft (the default) puts the
melody's loudest at 9, an echo at 4, chords at 6, a second line at 7, a
bass on a pulse or the sawtooth at 10 and the drums at 10, which with the
instruments' own envelopes is about where those tracks sit; style full
goes back to near full volume; `levels` sets channels by hand, each one's
loudest volume as written (a pad's 8 stays 8, whatever its envelope). A
noise channel of swells (wind, waves) keeps its volumes, and so do the
5B's envelope notes (a buzz plays the envelope at its own level whatever
its volume). A sawtooth
well over a triangle bass (or high up, with none) plays a second line or
chords, not the bass.

A note plays at its volume times its instrument's envelope (FamiTracker
multiplies them and divides by 15), so `mix` counts each part's loudest as
it plays: a pad whose envelope holds at 10 plays 10 at volume 15, and a
melody of it mixed to 9 gets volume 14. One whose envelope peaks lower than
its level can't reach it: its volume goes to 15 and the reply says how loud
it plays. A volume slide (`Axy`) that swells a part counts as high as it
goes. The slides move with the volumes, so a fade on a part mixed to half
its volume fades half as fast from half as high; a swell too slow to slow
down further (`A10`) stops where it would climb past the part's level, and
its volume column follows the original swell a step at a time.

The NES mixes the triangle, the noise and the DPCM together, so the DPCM
channel's level (`Zxx`) also sets how loud the triangle and the noise are:
`Z00` full, `Z20` about 80%, `Z30` 70%, `Z60` half, `Z7F` 43%. FamiTracker
plays that the way the NES does. The soft mix sets `Z30` where the song
starts and where its loop starts, so nothing sticks out of the quieter
pulses; `dac` sets the level alone. Samples on the DPCM channel start from
that level, and at a high one they have less room and can clip.

Those levels are as loud as on a pulse, and the chips don't count volume
alike. Measured in FamiTracker's own rendering, a VRC7 sound at 15 plays
about 5 dB louder than a 2A03 pulse at 15, and each step down is 3 dB
(the YM2413's attenuation), so 9 there is 18 dB under 15 rather than
4 dB; the soft mix puts a VRC7 melody at 12 and its echo at 10, and keeps
each part's accents and fades as many dB under its loudest as they were.
The Sunsoft 5B's squares count volume in steps of 3 dB too, and at 15 play
as loud as a 2A03 pulse at 15 (measured: 0.4 dB louder), so the soft mix
puts a 5B melody at 13. The N163 depends on its channel count: it plays
its channels in turn, each a share of the time, which FamiTracker's mixer
makes up in part, so a square wave at 15 plays 0.6 dB under a pulse with 4
channels and 4.4 dB over with 1 (measured under Wine). Its wave sounds'
waves are rounder than a square, 4.1 dB under it on average, so with 2 to 8
channels they play about 5 dB quieter than a pulse: an N163 melody stays at
15 and its other parts sit as far under it as the roles ask. With one
channel they play about as loud as a pulse. On the VRC6 Plus, the soft
layer's parts sit another 3 dB under their roles, under the VRC6. The FDS's sounds play about 5 dB louder than a pulse with their
envelope at its top (from 4.5 to 6.5 dB, sound by sound), so its parts sit
lower. The VRC6, MMC5 and 2A03 count volume alike. The echo, the harmony and
the counter-melody take their volumes the same way (an echo three steps
under a VRC7 or 5B melody, not nine; an echo counts both instruments'
envelopes too, so one on a softer sound gets a higher column), and
`polish` makes a VRC7 or 5B part's accents one step and its fades three.

`review_arrangement` mentions a mix where every part plays near full
volume, and a melody that plays well under another part (a VRC7 melody at
9 under the triangle, say). Tools that write parts afterwards use their own
volumes, so mixing is a last step, or one to repeat at the end. A channel
with a fader in FamiTracker's mixer (see [The mixer](#the-mixer)) is mixed
as it sounds with it: 3 dB down, its volumes come out higher to match. The
triangle's fader counts too, though it has no volume of its own.

Over some bars (`from_bar`, `to_bar`), each part keeps its level from the
rest of the song: the soft and full mixes scale those bars as they would
scale the whole song, by the part's loudest anywhere, so a quiet section
stays quieter ("loudest 10 to 6, as the song's 15 to 9"), and `dac` over
bars whose DPCM levels step (`Zxx` written on purpose) moves the steps
with it.

### Calm and lively tracks

A lead that holds a steady level from attack to release, 5 to 8 dB over the
chords, sounds direct, even aggressive, however soft the parts around it:
that was what you heard in the night songs, asked to be calm and
melancholic. Calm tracks (night versions, ambient music) keep the melody
only a little over the chords. Lively stages go the other way, as the
soundtrack's first lively stages were made (you liked nearly all of them):
a lead with accents inside a band louder than it, a sampled beat, sections
that build. `mix`, `polish`, `check_song` and `review_arrangement` take a
`style` for them.
Calm applies only when asked, or when the track's name says so ("Town
(night)"; night, calm, evening, sleep, peaceful, quiet or relaxed), and the
reply then says so in a few words; `style` `soft` (for `mix`) or `any` takes
it back. A song that only looks calm, about 90 BPM or slower with no drum
kit (no drum hits on the noise, no samples; a swell of wind on the noise
isn't a drum), gets a line suggesting the style, and nothing else changes:
the soft mix and polish's accents stay as they are, for action and bosses.

`mix` with `style` `calm` or `lively` mixes as heard. It estimates each
part's loudness as its stem would measure it, frame by frame from its
volume, its instrument's envelope, the pulse's width or the wave, the
fader, the pan and Soft, over 400 ms blocks gated as the meters gate them
(against FamiTracker's renders, within about half a dB), where the part
plays with the melody, and sets the volumes so that each part sits at its
place against the melody. A calm mix keeps the melody's level; a lively
one brings its loudest note to 9 as the soft mix does (as loud as 9 on a
pulse), scaling the whole part, so its accents and its steps by section
stay (`levels` sets it in either):

| Part | calm | lively |
|---|---|---|
| the chords | their loudest layer 2.5 dB under | all of them together 3.5 under |
| an echo of the melody | 7 under | 7 under |
| a second line or an answering voice | no louder than 3 under (to 4) | no louder than 2.5 under (to 3.5) |
| the bass | 2.5 under | 1 under |
| the drums | 10-12 under, never raised | the noise's hi-hats on their own 14 under (its kick and snare along), a noise without hats 11.5 under, up or down, left as written within 1.5 |

The lively places are the first lively stages', measured as heard: the
melody about level with its bass (1 dB under it), about 3 dB over all its
chord parts together, the echo 6.5 under and the noise's hi-hats on their
own 13.5 under (the noise as a whole about 10 under by a meter's windows,
its crashes and open hats along), so the band together played 3.3 dB
louder than the melody. Placing the noise as a whole put a later stage's
hats 8 dB under its lead. The later stages,
mixed with the melody kept at 15, the bass 3 under and the drums never
raised, had their lead 3.5 dB over the bass and as loud as the rest
together, over sections within a dB of each other. A calm track's drums
keep the volumes they were written at while they play 10 dB or more under
the melody (soft hi-hats at 4-7 stay soft); louder ones come down to 11
under. The reply says which, and why, gives the band together on a lively
stage ("the band together 2.1 dB over the melody"), and says when the
melody plays at one volume throughout (polish's accents first).
Other chord layers playing with the loudest one (a pad, the VRC6 Plus's
soft layer) keep their place under it, together at least 3 dB under it.
On a calm track the layers then move together, so all of them sit at the
chords' place, 2-3 dB under the melody, as the review and the stems hear
them (the main layer at the place and a pad 3 dB under it put them within a
dB of the melody); a song with one chord layer mixes as before.
On a lively stage each chord layer moves on its own: together they take
their place over the whole song, a layer that joins the one heard longest
only in some parts (pads at the climax) comes down to stay under it, and
the chords together stay at least 2 dB under the melody in every part of
the song (each named section, or four bars at a time; nearer, the review
hears them cover it there), the layers that join coming down first, then
all of them; the reply says where that held them. (One shift for every
layer, held back by a climax's pads, left a song's chords 5.7 dB under its
melody.) A second voice or a double that moves with the tune, or a steady
run of quick notes under it (a ripple of broken chords), is a line, not a
pad's voice; a part already within half a dB of its place stays as it is,
so a second lively mix leaves a mixed song alone.
The triangle, which has no volume, gets its place from the DPCM level,
unless `dac` is given (the noise moves with it, and the drums make up for
it). With samples on the DPCM channel the level goes no higher than Z40,
so under a lively lead at 9 the triangle can stay a little over it, as the
first stages' did. A second line quieter than its place keeps its level: it may be an
ornament (a bell that blinks, a bird). Over some bars the parts are set
against the melody there, the melody keeping its level from the rest of
the song. Volumes come in whole steps, and FamiTracker rounds a volume
times its envelope down (on a sound that holds 12, volumes 5 and 6 both
play 4), so each part off its place tries the volumes around its own and
keeps the one heard nearest. The reply gives the balance reached ("as
heard: the melody (VRC6 Pulse 1) 2.7 dB over the chords, the echo 6.6
under and the triangle 2.5 under") and names a part still half a dB or
more off, between two steps or at 15 or 1, with how far: a fader in
`set_mixer` moves it the rest. `review_arrangement` with the style hears the same balance: on a
calm track it tips a melody more than 4 dB over the chords (over all of
them together where they're in layers, as the stems measure them: the
balance gives the main layer and the chords together then) or under 1 dB;
on a lively stage, chords that cover the melody (under 2 dB under it), a
melody that stands over its band (the band together quieter than it, or
the bass more than 3 dB under it), a lead at one volume throughout, and a
song that doesn't build (its loudest section under 1.5 dB over the first A,
as heard; the first stages built 3.2 dB, the later ones 0.95). No lively
tip asks for a softer melody or fewer accents. `measure_mix` renders the
stems themselves.

`mix` given `width`, `levels` or `dac` and no `style` changes only those,
and its reply says so: give a style as well to balance the parts.

`polish` with `style` `lively` gives the default touches, accents and all (a
stage's lead is a step louder on a bar's first note); scoops and falls go
on chosen bars. With `style` `calm` it leaves out accents, scoops and falls (its
touches are vibrato, separate and clicks). Its vibrato is the shallowest
(`451`), coming in about half a second into each long note that still
sounds then, and none on an instrument with a vibrato of its own. Accents,
scoops or falls asked for are done, with a line saying they go against the
calm style.

`check_song` and `review_arrangement` with `style` `calm` warn about a lead
whose sound holds a high, steady level from attack to release: it reaches
its peak in under 10 frames and holds 11 or more of 15 (on the 5B, as loud
as that on a pulse). A swell of 10 frames or more, a strike that falls to 7
or under, or a sound that fades away doesn't; `design_instrument`'s kinds
breath, falling strike and music box are made that way. It's a finding of
its own (`held` in the JSON), on calm tracks only: a lively stage's lead
may settle to a level, as the first stages' leads settled to 10-12.

### Measuring the mix

`mix` and `review_arrangement` work from the volumes in the notes;
`measure_mix` measures what FamiTracker plays. It renders the selected track
the way `export_for_game` does, with FamiTracker's mixer, Soft and Space, and
changes nothing: no undo step, no change to the mixer or the track, and no
file written (the exported OGG files stay as they are).

- **Loudness, section by section:** each named section, or every 4 bars when
  the track has none (the intro's bars apart), in LUFS with the two sides
  together as BS.1770 sums them (as `export_for_game` and ffmpeg measure
  it), with its loudest peak in dBFS; then the loop's. The loudest section
  and the quietest after the intro are marked when they're half a dB apart
  or more.
- **The intro against the loop:** 6-10 dB under reads as an intro. More
  than 10 under, players in a game hear silence; less than 6, it doesn't set
  itself apart. The reply says when it's outside.
- **The two sides:** each side's level (RMS) and how far apart they are.
  More than 2 dB apart, it names the part that puts the most on the louder
  side.
- **Each part alone:** the melody, the chords together, and each other
  channel that plays with the melody, each rendered with the other channels
  silent. Their sound goes, but their notes and effects still play (the
  DPCM level still softens the triangle and the noise), and each part keeps
  its own Space, so the space holds only that part. A part is measured in
  the bars where it plays with the melody, against the melody there ("the
  melody 2.7 dB over"), with how that varies from section to section. The
  chords are the channels whose notes sound chords (arpeggio or lush
  instruments, `0xy`), or else voiced chords (two to four channels of one
  kind starting their notes together); `chords` names other channels.
  `parts` names what to measure instead: channels, `"chords"`, or channels
  joined with `+` as one part (`"Soft 1+Soft 2+Soft 3"`). `melody_channel`
  names the melody; otherwise it's guessed as `review_arrangement` guesses
  it, and the reply says so (a high part of a few notes can win the guess).
- **Targets:** `style` `calm` checks the melody 2-3 dB over the chords;
  `lively` a stage's band: the melody 3-4 dB over the chords together,
  about level with the bass (from 1 dB under it to 2 over: the first
  stages had it 1 under; with samples on the DPCM channel the triangle
  comes down no further than Z40 lets it) and 6-8 over an echo, and the
  band together (every channel
  but the melody's, a render of its own) louder than the melody. Each
  says how far to move the faders; `any`, the default, gives the numbers
  alone.
- **Short:** `format` `short` gives the loop, the intro, the sides and the
  four parts nearest the melody in a few lines, and what's off its target
  with the `set_mixer` call that moves it (see [Short replies](#short-replies)).
- **Loudness of the file:** with `loudness` (LUFS, as `export_for_game`
  takes it) every number is as the exported file at that loudness has it,
  and the reply says whether the loudest peak holds it back. Without it,
  the numbers are as FamiTracker renders (`export_for_game` with
  `normalize` false).
- **Cues and sound effects:** a song that halts is measured to the bar it
  halts in. A sound effect (speed 1, as `make_sound_effect` writes one, a
  long loop of ambience too) is measured as a whole, a sound shorter than
  400 ms over its whole length as `export_for_game` measures it, with each
  channel alone.
- **Without a melody** (ambience: a rain loop, wind): when no channel plays
  a tune, or the likeliest only plays a note now and then (heard for under a
  fifth of the bars it plays in, and fewer than a note and a half a bar
  there: drips, plinks), or `melody_channel` is `none`, each part is
  measured alone against the whole where it plays ("4.6 dB under the
  whole"), and the reply says why. `parts` names the parts then too.
  `check_song` checks no key in such a track (a rain's drips fall at any
  pitch) unless one is chosen for it or given.
- **Roles:** a part is an echo only when it follows the melody's notes (one
  delay, notes about as long as the ones it copies), so a pad holding a
  note the melody touched isn't one; a quiet arpeggio over the melody (a
  sparkle) is a line of its own, not the chords; and a low, held line on an
  N163 voice (the soft layer's sub bass) is the bass, not one of the pads'
  voices even where it starts its notes with them. Plucks that start a row
  apart ("pah-pi", a chord broken over two plucks) are voices as plucks
  struck together are, so a song whose plucks start together in some parts
  and a row apart in others has them as the chords over the whole song as
  section by section. Voiced chords (plucks on
  two pulses, a pad's voices) are the chords beside a quiet arpeggio too:
  `measure_mix` then gives the chords together, the arpeggio and the
  voices. A triangle that thumps now and then (short notes, heard for less
  than half as long as the sawtooth's line) beside a sawtooth's bass isn't
  the bass. `mix` and `review_arrangement` hear the parts the same way.

FamiTracker renders the mix and every part alone together, in one pass
through the song without the space's tail after, and each part comes out
exactly as a render of it alone would (a song of more than 16 parts takes
another pass). Each part makes that pass a little slower than a render of
the mix alone, but it's much faster than rendering the parts one by one,
as earlier versions did: a 40-bar song with nine parts takes about half
the time. `stems` false measures the mix alone, and `parts` `["chords"]`
checks the melody against the chords alone.

### Listening to the song

`listen` hears the selected track as FamiTracker plays it, which the notes
alone don't say: FamiTracker renders the song's real audio, the mix and each
part alone in one pass (as `measure_mix` does, changing nothing), and the tool compares what
each part sounds like with what its notes say it should:

- **Warbles and deep vibratos:** a held note whose pitch shakes faster than
  8 times a second and wider than 40 cents either way is a warble, heard as
  a short right note and then a fluttering echo (the FDS's modulator at
  speed 34 and depth 5 shakes a note about a semitone and a half either way
  14.5 times a second); a slower vibrato wider than 60 cents either way
  sounds out of tune rather than sung, a detail under a semitone.
- **Out of tune:** held notes 20 cents or more from where they should sound,
  counting each chip's octaves (the FDS two under the note written, the
  triangle and the 5B one, an N163 wave by its cycle), the fine pitch (`Pxx`)
  and the chip's own rounding (said so when that's the reason).
- **Parts beating:** two parts on one pitch, or octaves apart, 12 cents or
  more off each other, so together they waver (a chorus a few cents apart
  passes).
- **Clicks and pops:** clicks inside held notes on the 2A03's and MMC5's
  pulses (whose wave restarts when a vibrato takes the period across a
  multiple of 256), pops as notes start on a smooth wave, a pop at the
  loop's seam.
- **Sound that holds on** after a release or a halt, and **notes hardly
  heard** in their own part.
- In the mix: **clipping**, **jumps in loudness** where no section starts,
  and **harsh** or **muddy** stretches (more of the sound from 2 kHz up, or
  from 160 to 500 Hz, than 99 in 100 of the soundtrack's own bars).

Each finding says in plain words what's heard, on which part and in which
bars, how sure it is and how much a listener notices it (high, medium or
low), with the call that fixes it where a tool has one (a warble's is
`add_vibrato` with `modulator` `off`) or what to change. A low one is a detail
that may be meant (a slow vibrato a flute was given on purpose), so it carries
no call: it says to play those bars with the user and change them only if
they'd like it gone, and what would change it. `from_bar` and
`to_bar` listen to some bars, `parts` to some channels; `format` is `text`,
`short` (the three heard worst) or `json`. It takes a while, since
FamiTracker renders the track first: a two-and-a-half-minute track with
eight parts took about 40 seconds under Wine. A local model once told the user
it couldn't actually listen to the song (GitHub issue #9), so the tool's
description, every reply, `what_next`'s listen step and the small set's
instructions say it plainly: `listen` renders the real audio and reports what
an ear would notice, and the user's own ear has the final word (it hears how
the song sounds, not whether it's the music they want). `play` and
`compare_versions` say they play for the user's ears and point to `listen`.
FamiTracker's **View > Problems** has a **Listen** button (F6 in the panel)
that runs it and lists what it hears with `check_song`'s findings, each
with Play it; `finish_song` listens too, after its mix.

## Stereo

This FamiTracker plays in stereo, which the NES never did: each channel can
play from the left, the middle or the right, set by the pan effect `8xx`
(`800` all the way left, `880` the middle, `8FF` all the way right, `840`
halfway left). A pan lasts until the channel's next `8xx`, and every
channel starts in the middle, so a song without `8xx` sounds exactly as
before, the same on both sides.

Ask in words: "put the echo on the other side", "spread the chords a
little", "pan the hi-hats left and right", "make it wider for the game".
Claude places single notes with `write_notes` (a note's `pan`), a part in
some bars with `set_pan` (also `alternate` and `spread`), an echo on the
other side of its melody with `add_echo` (`pan` "opposite"), and the whole
picture with `mix` (`width` wide).

Where it helps is parts that compete: an echo on the other side of its
melody stops blurring it; chord voices or arpeggios a little apart; a
counter-melody answering from the other side; hi-hats alternating. Keep the
melody, the bass, the kick and the snare in the middle: they carry the song,
and some players hear only one speaker. Spread a little (halfway) rather
than hard left and right; on headphones a part far to one side tires.

The tools keep the pans when they write: a note written after a panned one
plays where the channel is panned, copies (`copy_bars`) and variations keep
their places, the note after a change keeps its own, and when a change
leaves the song's end panned otherwise than the loop's first note plays the
first time through, that note gets its own pan written (the reply says so),
so every repeat starts like the first time. `get_song`, `read_notes`,
`look_at_song` and `review_arrangement` say where the parts play
(`get_song` and the review with the mixer's starting pans in).

You hear stereo in FamiTracker's playback, in File > Create WAV and in the
game's OGG files (`export_for_game`, whose loudness is measured over both
channels, as EBU R128 does). NSF files and the NES are mono: NSF export
leaves `8xx` out. FamiTracker 0.4.6 opens a song with pans, shows
"(invalid)" on rows with `8xx` in their first effect column and plays
everything in the middle.

In FamiTracker itself, type `8` in an effect column, or select notes in
the piano roll and pick **Effects > Pan (stereo)**: Left, Halfway left,
Center, Halfway right or Right. A panned note shows an arrow at the end it
leans to.

## The mixer

**View > Mixer** opens a strip for every channel of the song along the
bottom of the window, in channel order (the 2A03's, then each expansion
chip's that's on). Each strip has the channel's name and chip, a meter
that moves while the song plays, **M** and **S** (mute and solo, the same
as clicking the channel's name in the pattern editor), a fader from -24 to
+6 dB and a pan from left to right.

- Drag the fader (Shift for tenths of a dB) or the pan; double-click one to
  put it back at 0 dB or the middle, or right-click a strip. Each change is
  one Ctrl+Z, a drag one for the drag.
- The faders and pans are saved with the song, the same in every track.
  FamiTracker's playback, File > Create WAV and File > Export for games
  play them; they aren't changes to the notes.
- A pan is where the channel starts, before any `8xx`; an `8xx` still moves
  it from there, and every time the song starts again it starts there.
- NSF files play the notes as written, so NSF export leaves the mixer out
  and says so. **Write into the notes** (the button, or Claude's
  `set_mixer` with `write_into_notes`) puts it into every track: each
  fader into the channel's volume column, as `mix` scales a part, and each
  pan as `8xx` on the channel's first note. Then the faders go back to 0 dB
  and the middle, so the NSF sounds as the mix did, give or take a
  volume step. One Ctrl+Z undoes it all.
  - The triangle and the DPCM have no volume column: their faders stay in
    the mixer.
  - A channel raised past its loudest notes' volume 15 can't play louder
    in its notes. Then every channel comes down by that much instead,
    which keeps the balance (the song plays a little quieter).

Claude sees the mixer in `get_song`, sets it with `set_mixer`
("put the triangle 3 dB down", "start the echo channel half left"), and
counts the faders when it weighs how loud parts sound (`mix`, the review,
the echo and the harmony). For a mix that NSF files keep too, the notes'
own volumes (`mix`) are the better place; the mixer suits quick balancing
while you listen, and the game's OGG files.

### Soft and Space

Under each strip's pan are two knobs, **Soft** and **Space**, both off (0)
unless you turn them up to 100. They add a little depth that the NES never
had, so they're optional and meant for the game's OGG files: FamiTracker's
playback, File > Create WAV and File > Export for games play them, NSF files
never do (NSF export says so), and with both at 0 everywhere the sound is
exactly what it was.

- **Soft** rounds off a channel's edges with a gentle low-pass, from barely
  there at 1 to warm at 100 (about 7 dB off the sound over 2 kHz). A pulse
  or a sawtooth still sounds like that chip, never muffled: at 100 it's
  still brighter than a triangle at the same pitch.
- **Space** sends that much of the channel (after its fader) to the space
  all channels share: a reverb in a room, with an echo, coming back on both
  sides. Choose the room, the echo and its repeats at the top of the mixer:
  a small room, a hall, a cathedral, a cave, or none (the echo alone); the
  echo in beats of the playing track's tempo (off, 1/4 to 2 beats) and how
  many times it repeats (1-8, each quieter). One space for the whole song.
- Drag a knob up or down (Shift for finer steps), turn it with the mouse
  wheel, or double-click it to turn it off. Each change is one Ctrl+Z, a drag
  one for the drag; right-click a strip to turn both off.
- Use them sparingly: a little Space on pads and chords, a lead or an echo
  in a big place like a cave or a cathedral, Soft on a pulse or a sawtooth
  that should sound warm. Leave the bass and the drums dry.
- The space rings on after the notes: when a song stops, File > Create WAV
  keeps rendering until its tail has died away, and **Export for games**
  takes the loop from the song's second time through, whose start already
  has the tail of the end, so the loop's jump back keeps the echo going. A
  song that halts keeps its tail.
- Write into the notes leaves Soft and Space in the mixer: notes can't hold
  them.

Claude sets them with `set_mixer` (`soft`, `space`, and `room`, `echo`,
`repeats` for the space: "a little space on the pads in a hall", "put the
lead in a cave with a half-beat echo") and counts each channel's level
before them in `mix` and the review.

## Expansion chips

Ask for "a VRC6 song", "a VRC7 song", "a 5B song" or "VRC6 and N163" and
Claude sets the expansion chips with `set_expansion` (`chips`: a list, or
`chip`: one), as **Module > Module properties** does with its checkboxes. A
song can have any of the VRC6, VRC7, FDS, MMC5, N163 and Sunsoft 5B on at
once, for every track; their channels follow the 2A03's in the order VRC6,
MMC5, N163, FDS, VRC7, 5B. The channels of the chips that stay keep their
notes (they move to their new place); the channels of chips switched off go,
and new ones start empty:

- When the channels that go have notes, Claude asks you first, since
  they're cleared and FamiTracker can't undo switching chips (Module >
  Module properties switches back, without the notes). Save a copy first if
  you might want them. Fewer N163 channels count the same way.
- Speed changes, loops and ends (`Fxx`, `Bxx`, `Dxx`, `Cxx`) on those
  channels move to the song's last channel, so the song still loops the way
  it did.
- Expansion chips play on NTSC machines, so a PAL song becomes NTSC.
- FamiTracker's NSF export plays one expansion chip, and not the 5B, so it
  refuses a song with several chips (the VRC6 Plus is two) or the 5B,
  saying why; `export_for_game` (OGG) and WAV export play every chip.

**Instrument > Sound browser** also lists the ready-made sounds of the chips
a song doesn't have, after its own under "VRC7 (not in the song yet)" and
the like, folded until you open them or search, so a sound can be found
before its chip (`find_sound` lists them with `other_chips`). They can't be
heard until the chip is in: Add to the song asks first, saying what the chip
brings (its channels come after the song's own), that every note stays and
whether NSF export can still play the song, then adds the chip (the N163
with 4 channels, the VRC6 Plus with its starter sounds) as Module > Module
properties does, and the sound with it. As there, Ctrl+Z doesn't take a
chip back, and adding one clears the undo history, so the changes made
before it can't be undone either (the question says so); Module properties
takes it off again. A chip that would change
the song's channels (the VRC6 Plus on a song with the N163) isn't offered.

The **VRC6** adds two pulses and a sawtooth, and the **MMC5** two more pulses
(FamiTracker has no MMC5 sample channel). The presets work on all of them:
the MMC5's pulses play the 2A03's instruments, so they also take chords,
echoes and the parts of versions and jingles like the 2A03's pulses.

The **VRC7** has six FM channels with 15 sounds built in: bell, guitar,
piano, flute, clarinet, rattling bell, trumpet, reed organ, soft bell,
xylophone, vibraphone, brass, bass guitar, synthesizer and chorus. Claude
adds them as instruments by name ("a piano for the melody", "brass chords"),
and a preset's name picks the sound most like it: "soft lead" is the flute,
"bass" the bass guitar, "warm pad" the chorus. Notes end with a halt (`---`),
which lets the sound fade the way the chip does. Like the triangle, the VRC7
sounds an octave under the note written (its A-3 is 220 Hz, a pulse's 440),
so a part that plays with a pulse is written an octave up there; the echo,
the harmony, the counter-melody, voiced chords and a jingle's harmony are
written that way, and the checks and the review count VRC7 notes where they
sound.

The chip also has one custom patch, whose 8 registers set two FM operators
(their frequency ratio, the modulation level, feedback and envelopes).
Claude has six of its own design, measured with the same emulator
FamiTracker plays: electric piano (a bright, bell-like attack that mellows
into a warm tone, for comping chords), soft electric piano, glass bell, fm
pad (swells in and holds, with a gentle vibrato), fm bass and wooden
mallet. Ask for them by name ("rhodes" and "epiano" work too). The chip
holds one custom patch at a time, so two channels playing different custom
patches at once both sound like the one struck last; Claude says so when a
song gets a second one. Two channels playing the same custom patch are
fine, which is how an electric piano comps chords across them.

The **N163** (Namco 163) adds 1 to 8 wavetable channels, 13 in all with the
2A03's, and Claude draws their sounds: soft flute, drawbar organ, string
ensemble, choir, brass section, electric piano, glass bell, harp, marimba,
pulse sweep lead, growl bass and round bass, and the VRC6 Plus's choir pad,
string pad, sub bass, breath, quick pad, pulsing pad and music box. Each
plays waves of its own one
after the other, so its timbre moves as a note plays: the organ's upper
drawbars turn like a rotating speaker, the strings and the choir shimmer,
brass opens up from mellow to bright, the electric piano and the bell mellow
as they fade, the lead's pulse narrows and widens smoothly, and the bass
growls like a wah. A preset's name picks the sound most like it ("soft
lead" is the flute, "bass" the round bass). A song holds up to 8 of them:
FamiTracker 0.4.6 loads an N163 wave only from the first 128 samples of the
chip's memory (a wave placed later makes the module load out of step), so
Claude puts each sound's 16-sample wave there, in a place of its own.

- The waves are 16 samples long: FamiTracker 0.4.6 plays N163 notes in tune
  at that length (a 32-sample wave sounds an octave lower than written).
- All waves share the chip's 128 bytes of memory with the channels'
  registers, 16 samples for each channel in use: with 6 channels there is
  room for 10 sounds, with 8 for 8. Claude puts each new sound's wave where
  no other N163 instrument's is, from the bottom up, and says how much room
  is left.
- The chip plays its channels in turn, which whines at a pitch that falls as
  channels are added: up to 6 keep it above hearing (about 20 kHz), 7 and 8
  bring it down to 17 and 15 kHz. FamiTracker plays it too, so it's in the
  exports.

### VRC6 Plus

The **VRC6 Plus** is this FamiTracker's own: the real VRC6 (its two pulses
and its sawtooth) with a soft layer of four wave channels played by the
N163, for when a VRC6 song should be a bit softer, warmer or deeper. It
plays only soft waves through the N163's emulation, so it still sounds like
the NES. Ask for "a VRC6 Plus song", or for something softer or deeper on a
VRC6 song, and Claude sets it with `set_expansion` (`chips`:
`["VRC6 Plus"]`, or with another chip beside it); **Module > Module
properties** has a box for it.

- It's one chip: its channels are Pulse 1, Pulse 2 and Sawtooth of the VRC6
  Plus, then Soft 1 to Soft 4, in the pattern editor, the piano roll, the
  mixer, the problems panel and the tools ("VRC6 Plus Soft 1"). The tools
  still take the soft channels' N163 names ("Namco 1", "N163 Namco 1") and
  the pulses' VRC6 ones.
- The VRC6 carries the music (the melody, the sawtooth's bass and bite);
  the soft layer sits underneath, quieter and optional: a pad or a choir
  holding the chords (`add_chords_and_bass` with `chord_style` `voices`
  puts them on the soft channels, the string pad by default), a soft echo
  (`add_echo` onto Soft 1 with the soft flute), a sub bass an octave under
  the sawtooth. Left empty, the song is plain VRC6. `mix` keeps the soft
  layer 3 dB under its parts' roles. The style guide's topic `vrc6-plus`
  (`references` with action `guide`) has more.
- Choosing it adds what's missing of its starter sounds, each with a wave
  of its own under sample 128: choir pad, string pad, soft flute and sub
  bass. `find_sound` and the sound browser list them first, under "VRC6
  Plus soft layer", then its calm shapes, then the N163's other sounds.
- Its calm shapes, added by name on a soft channel (a song holds 8 soft
  sounds, the starters among them): **breath**, a breathy lead that comes
  in from 4 over 11 frames, sinks to 10 and holds, with a faint vibrato;
  **quick pad**, in within 7 frames, for a chord every beat (the starter
  pads take 20-24 frames to swell); **pulsing pad**, struck again every 12
  frames, each strike softer down to one it repeats, with a soft echo after
  a release (the slow pads of atmospheric FamiTracker music); **music box**, a bright tine that
  mellows to a pure tone and is gone in 40 frames.
- How loud they play at volume 15 against a 2A03 pulse at 15 (measured,
  with the soft layer's 4 channels): choir pad 8.3 dB under, string pad
  7.3, soft flute 5.3, sub bass 5.1, breath 7.4, quick pad 7.7, pulsing
  pad 10.4 over its strikes, music box 4.8 at its strike. `add_instrument`
  says it when it adds one.
- `design_instrument` makes sounds of your own there: a wave sound's waves
  (`wave`, or the kind's: the soft flute's for a lead, the string pad's for
  a pad, the breath's and the music box's for theirs) with the envelope,
  vibrato and pitch you describe, each with a wave place of its own.
- The sounds of your reference library play there too: `reference_sound`
  with a soft channel makes a studied lead, pad or bass a wave sound with
  the original's envelopes. An N163 game's or module's sound brings its
  own waves and the sequence that steps through them (studies keep them
  now; study a track again for one studied before), each wave resampled to
  the 16 samples that play in tune; another chip's sound gets a wave from
  its channel: its duty's pulse (a wave for each width a duty click steps
  through), the triangle, the sawtooth, or an FDS sound's wave. Each takes
  a wave place of its own, so several play together without touching each
  other's waves, and the reply says how loud it plays, as `add_instrument`
  does. A triangle sound sounds as written there, an octave over where the
  triangle played it, and so does an N163 sound whose wave wasn't a cycle
  of 16 samples (a cycle of 32 sounded an octave under its notes): the
  reply says how far, so you can keep its register. `find_sound` and the
  sound browser list the studied sounds under the soft layer too (an N163
  sound at home there).
- Underneath, the song is VRC6 + N163 with 4 channels, marked by a
  `BF VRC6PLUS` block, so other FamiTrackers open it as that and play it
  the same. Back to the VRC6 (`chips: ["VRC6"]`) clears the soft channels
  after asking; `["VRC6", "N163"]` keeps them as plain N163 channels.
  NSF export plays one chip, so a VRC6 Plus song goes to a game as OGG
  files.

The **Sunsoft 5B** adds three square waves (always 50%), each of which can
also play the chip's noise and its envelope. Claude has sounds of its own for
it: square lead, singing square, square pad, square pluck, square bell, buzz
bass, sync buzz, noise hat, noise crash, noise snare, square kick and square
tom, also in **Instrument > Sound browser**. The presets play there
too ("soft lead", "warm pad"), their volume envelopes turned into the 5B's
3 dB steps so they sound as loud as on a pulse; "snare", "hi-hat" and
"kick" there are the 5B's own drums. Like the triangle and the VRC7, the 5B
sounds an octave under the note written, and the tools that write parts
for it allow for that.

- The buzz bass plays the chip's envelope as a falling saw at the note's
  pitch: a strong, bright bass. Until a song sets the envelope with `Hxy`
  (`y` the shape, `x` how its speed follows the note: `C` at the note's
  pitch), `Ixx` and `Jxx` (its period), envelope notes play `HC8`. The
  envelope sets the level, so the volume column only silences it (at 0),
  and the three channels share the one envelope: one buzz at a time.
- An instrument's duty sequence is the 5B's noise and mode a frame at a
  time: 64 the square, 128 the noise, 32 the envelope, added up, and the
  noise's pitch (0-31) in the low bits; `Vxx` sets the mode for notes
  without one (1 the square, 2 the noise, 4 the envelope).
- A note that plays the noise alone (by its instrument's mode sequence, or
  `V02`) has no pitch you hear: `check_song` leaves it out of the key, the
  range and the clashes, and the tools leave it out of the key they find,
  the chords they hear and the melody they guess. `design_instrument` makes
  such sounds at a pitch (`noise_pitch`, see [Sounds in words](#sounds-in-words)).

### FDS sounds

What a modulator does, `study_nsf`'s studies, `reference_sound` and
`design_instrument` say in plain words, as FamiTracker's emulator bends the
pitch: how many times a second and how far, and whether that's a vibrato or
a warble (fast and deep: each note sounds like a short right note and then a
fluttering echo). `check_song` warns from the notes alone when FDS notes are
held where a warbling modulator plays (the instrument's own, or brought in by
`Hxx`, `Ixx` and `Jxx`), with the `add_vibrato` call that replaces it with a
gentle vibrato; a slow, gentle one (depth 1 at speed 7, three times a
second) stays quiet.

The **FDS** (the Famicom Disk System) adds one wavetable channel whose
modulator bends its pitch, and Claude draws its sounds: soft flute, whistle,
eerie lead, glass bell, warm organ, nasal reed, swelling strings, choir pad,
round bass, growl bass, underwater wobble and cave drone. Each is a wave of
64 samples, a volume envelope (the FDS counts 0-31) and, where it makes the
sound, a vibrato or the modulator:

- The flute, the whistle, the reed, the strings and the choir sing with a
  vibrato that comes in after a moment, and the organ has a faint chorus.
  The strings and the choir swell in; the cave drone breathes slowly in and
  out as it holds.
- The eerie lead holds its note for half a second, then its pitch trembles
  widely five times a second, like a theremin in the dark. The underwater
  wobble wobbles three times a second from the start.
- The growl bass's modulator shakes its pitch 35 times a second, so it
  growls; the cave drone's 26 times, a faint rumble.
- The glass bell strikes two octaves and then one octave above its note,
  a frame each, rings and dies away over a second and a half.

Ask for them by name, or by a preset's name, which picks the sound most like
it ("soft lead" is the flute, "bass" the round bass, "warm pad" the choir).
**Instrument > Sound browser** offers the same sounds for the FDS
channel. `design_instrument` makes new ones there from words: a wave (sine,
round, hollow, square, bright, strings, nasal, organ, voice, glassy, bass,
triangle), the envelope, attack and vibrato as for the other chips, and what
the modulator does (wobble, tremble, growl or rumble, from a moment into the
note if asked).

Measured in FamiTracker's own rendering against a 2A03 pulse at 15 playing
the same notes where they sound, they play about 5 dB louder with their
envelope at its top, so `mix` sets FDS parts lower. A wave of many
harmonics in phase peaks high with little power in it, so the organ, the
reed, the strings, the choir, the basses and the drone have their harmonics'
phases picked for the most power: up to 5 dB louder at the same volume, the
same harmonics.

- FamiTracker 0.4.6 plays the FDS two octaves under the note written: its
  A-5 sounds 440 Hz, as a pulse's A-3 does. Every FDS instrument does,
  also the ones made in FamiTracker's instrument editor, whose waves have
  one cycle as these do. Claude writes FDS notes two octaves up, the echo,
  the harmony, the counter-melody, the chords and the bass do so by
  themselves, and the checks count FDS notes where they sound.
- The FDS plays up to G#7 (sounding G#5): its pitch register tops out
  there, so A-7 plays 13 cents flat and A#7 and B-7 no higher. `check_song`
  lists such notes, and ones under A-1, which sound under 28 Hz.
- 0.4.6's modulator rounds each bend away from the note, in 64ths of its
  pitch: the smallest bend it makes is about 55 cents down or 79 up,
  whatever the depth (FamiTracker's own "sine" table at depth 1 wobbles
  between the note and a quarter tone under it, so the note sounds flat).
  So these sounds' vibratos are pitch sequences, and their wobbles take
  turns between those two smallest bends, which stays in tune on average.
- `Hxx`, `Ixx` and `Jxx` on the FDS channel set its modulator's depth and
  speed as a part plays.

A study of a Famicom Disk System game (`study_nsf`, or `study_module` on an
FDS module) keeps each FDS sound's wave and modulator with its envelopes,
and `reference_sound` makes the instrument again on the FDS channel as it
was (in an FDS song also when no channel is given). On a pulse an FDS
sound's volume is counted 0-15 from the FDS's 0-32, its pitch sequence in
the pulse's coarser steps, and it plays the duty closest to how bright its
wave is. Another chip's sound on the FDS channel plays its duty's pulse, a
sawtooth or a triangle as its wave, its volume counted 0-31. References
studied before the waves were kept give a round wave: study the NSF again
for its own.

## Arrangements with depth

Tracks sound plain when every bar is the same: one chord held as an
arpeggio, a bass on the roots, no drums, a melody of one note length, a
section pasted again unchanged. The connection tells Claude how arrangers
avoid that, and gives it tools for each layer:

- **Chords** (`add_chords_and_bass`): by name ("| Am7 | Fmaj7 | C | G7sus4
  G7 |") or as degrees of the key ("I vi IV V", "V7/V"); "." holds a chord
  for a beat, "%" repeats a bar, and the progression repeats to fill the
  bars. Or fitted to the melody, hearing the other lines that play on with
  them (a counter-line, a second voice, chords or a bass left playing) at
  half a melody note's weight. A note from outside the key that they sound
  for a beat or more and that leads a fifth down into the next chord (a
  counter-line's C# before Dm in F major) makes that bar's chord the
  dominant it calls for (A, as the song's A7), unless the melody rubs
  against it; where no chord fits such a note, the bass plays another note
  of its chord there rather than rub against it, and the reply names those
  bars. Fitted chords take `color` sevenths (Imaj7, ii7, V7...) or
  rich where the melody doesn't rub against them: colors that keep each
  chord's part, add9, maj7 or 6 on the tonic and the subdominant, m7 and m9
  on minor chords, add9 on the V (its own seventh where that fits better),
  and a seventh that pulls only on a dominant, so each chord stays one chord
  (a V doesn't turn into a V7sus4 and its V7). Played as an `arpeggio`
  (the 0xy shimmer, held or struck on a rhythm),
  `stabs` (short hits: offbeats, eighths, tresillo, charleston,
  waltz...), `broken` chords (up, down, updown, alberti, rolling), a
  `guide` line (one long note a chord on its third or seventh), or `lush`
  chords: an instrument for each chord whose arpeggio loops through four or
  more of its notes, two frames each (`arp_frames`), so sevenths and ninths
  ring, played softly (volume 6) and colored rich unless asked otherwise.
  Their instruments ("Lush maj7", "Lush m9") are made when first needed
  from a preset's sound (lush chords by default) or from one of the song's
  own 2A03 or VRC6 instruments that `chord_instrument` names, by name or
  number (a pad you designed: each chord gets a copy of it, its volume,
  pitch and duty sequences kept, playing the chord's notes); `pattern`
  turns their arpeggios down, up and back, leaping (alberti) or spread
  over two octaves
  (rolling). They need a 2A03, MMC5 or VRC6 pulse or a 5B channel, since
  VRC7 instruments have no arpeggios.
  Or `voices`: the chord's notes on 2-4 channels of their own
  (`chord_channels`, else free FM channels, then N163 ones, then 5B ones,
  then pulses),
  the third and seventh first, each voice moving to the next chord's
  nearest note without crossing: an electric piano comping on the VRC7's
  FM channels (its custom patch), held or struck on a rhythm, each voice a
  frame after the one below it (`strum`, `Gxx`) as a hand rolls a chord.
  On the N163 each voice plays a sound of its own: a sound whose wave
  changes as it plays rewrites its waves in the chip's memory, so two
  channels playing it at once play each other's waves. On the VRC6 Plus's
  soft layer the voices take the string pad, the choir pad and the string
  ensemble, lowest first; a sound `chord_instrument` names gets copies
  ("Choir pad 2") with their waves at places of their own. `add_echo`,
  `add_harmony` and `add_counter_melody` onto an N163 channel do the same
  when another N163 channel plays their sound there (a sound of one wave
  can be shared).
  Every style keeps out of the melody's way and the bass's: each chord's
  notes stay a minor third under the lowest melody note sounding with it (a
  whole step where the chord's shape fits no lower, never a semitone) and
  over the bass's highest note, never in the bass's octave; broken chords
  on a pulse stay over C-2 however little room the melody leaves them
  (lower, a ripple turns to mud), and so do lush chords' roots (a rolling
  arpeggio spans two octaves from them). Chords asked for over a bar or two
  keep to the register the whole song gives them: it follows the song's
  melody (in bars where it rests, a low part playing alone there isn't
  taken for it, and the melody's few notes there don't move it), and
  bars the melody doesn't play in (an intro, a break) keep the chords' own
  register rather than one under the low notes the melody dips to. Voiced
  chords sit in the octave under the tune, and where no bass holds a chord's
  root (bars before the bass comes in) the lowest voice takes it. Voiced chords
  sit where they sound on the VRC7 too (it plays an octave under its
  notes), a voice gives way where the melody passes through it, and no two
  voices sit a semitone or a minor ninth apart where another voicing has
  none.
  Chords asked for over bars that have chords and a bass line already (other
  chords for a section) take their place, on their channels (voiced chords
  on the voices' channels), rather than stacking a second part on them.
  Chords given for more bars than the melody plays (an intro of chords
  before it) start early enough to end with the melody.
- **Bass** (`bass_style`): root_fifth, roots, half, octaves, walking (with
  approach notes into each chord, none grinding a semitone against the
  chord it walks under, a held V at a half cadence too), syncopated,
  arpeggio, driving, pedal. The walking and syncopated lines' passing notes
  keep off a semitone from the chord's notes (and a semitone under one,
  since broken chords and arpeggios play every note over the bass) and from
  the other parts' notes sounding with them (the melody's, a counter-line's),
  as `check_song` hears a clash. On
  the triangle it sits where it sounds like a bass (the triangle plays an
  octave below what's written).
- **Drums** (`add_drums`): basic, four_on_floor, half_time, shuffle, march,
  breakbeat, calm, driving, waltz, lively (for a gentle place: soft
  hi-hats, a soft kick, no snare; a stage's beat is basic with `kit`
  samples); light (fewer kicks and snares, the
  hi-hats kept), normal or busy; an open hi-hat into
  every other bar, fills that take turns (toms, a snare roll, kick and snare)
  and crashes. With `kit` samples (also in `make_groove`) the kick, the snare
  and the fills' toms are the sampled drums of `add_sample_drums` on the DPCM
  channel, and the noise channel keeps the hi-hats on the rows where it would
  otherwise give them up to a kick or a snare; `mix` writes its DPCM level on
  each sample, which would otherwise start it from silence. Each sample ends
  at the level it started from, whatever level that is, so the hits don't
  click and the bass doesn't jump between them (the toms of the first kit
  slid to silence when they started higher; a song with that kit has its
  samples brought up to date when the sampled drums are used again).
- **A second line**: `add_counter_melody` or `add_harmony`, quieter, on a
  free channel. In a calm track the counter-melody takes `style` calm, so
  the melody's rests stay rests but for a short answer in the long ones.
- **Finishing touches**: `polish` (accents, a vibrato that waits, gaps
  between repeated triangle notes, a nudge for notes that click; when asked, fades on held notes, `A0y`
  from halfway through, scoops into held notes after a leap up, falls (a
  sigh of two semitones, `R42`) where phrases end, and a staccato bass that
  bounces);
  `make_groove` starts a whole backing for a mood; `copy_bars` and `vary_part` build a form whose repeats change,
  and `insert_bars` and `remove_bars` make room for a section or cut one; `name_section` names the parts as
  the form grows (Intro, A, B, Bridge), and the tools then take a section's name in place of bars.

`review_arrangement` listens the way an arranger would and scores the depth
out of 10, with what would help most and the tool for each. Claude runs it
before calling a track done.

A track also needs to breathe. Rendered side by side, well-arranged NES
tracks open 8-10 dB under their main sections, drop for a breakdown and
come back, where a track whose parts all play every bar keeps one loudness
from start to end. The review estimates each two bars' loudness from the
parts that sound in them, at their volumes as each chip plays them, and
says how far the song swings; under 3 dB it asks for an intro and a break.
The triangle is loud, so bars with the bass stay loud: an intro on the
chords alone plays about 10 dB under the rest, with the bass only 3.
`compose_track` builds that in: an intro of a few bars with one or two
quiet parts (`insert_bars` at bar 1, `copy_bars` with `channels`, and
`set_loop` from the bar after, so it plays once), and a few bars without
the drums or the melody before a repeat. An intro makes the loop's seam
matter: a fade or an echo at the loop's end carries into its start on
repeat, where the first time came from the quiet intro, so the recipe ends
with `check_loop` and `fix_loop`.

## Sounds in words

`design_instrument` makes an instrument from words, starting from a kind
(lead, pad, pluck, bass, keys, bell, brass, strings, organ, chiptune; kick,
snare, hihat, open hihat, crash, tom) and changing what should differ:
attack, decay, sustain and release (words or frames, 60 a second; a decay of
40 frames reaches its sustain level, or silence, on frame 40; the decay's
words go by the kind, so a long bell rings twice its usual 40 frames), the
pulse width (12.5%, 25%, 50%; on the VRC6's pulses every 6.25% up to 50%,
such as the 37.5% of Castlevania III's night leads) and whether it opens
up, shimmers or thins as it fades, a vibrato built in that comes in after a
delay given in beats (faint, gentle, medium or strong: about 3, 20, 40 and
60 cents either way at A-3), an octave blip, a scoop or a fall on the
attack, or a chord played as a fast arpeggio from every note (major, minor,
sus2, sus4, 6, m6, maj7, m7, 7, add9, m(add9), diminished, m7b5, dim7,
augmented). A sound that holds under its peak (a pad at half) swells to that
level, or settles to it, rather than dropping straight after its attack.
`words` takes the sound as you'd say it ("a plain 50% square that holds at
full volume", "a soft pad with a gentle vibrato"): a kind, plain (nothing
added: no vibrato, duty motion, click, attack pitch or echo, struck at
once), held (no decay: at its peak while the note lasts), its loudness
(full, loud, soft, quiet, volume 12), a duty (50%, square, thin, bright,
round), a vibrato (none, faint, gentle, medium, strong), an attack
(instant, slow, swells in), dies away or rings, a long or short release, an
echo, a click, darker or brighter. What the words say stands where the
kind's defaults would, an argument given wins over the words, and the reply
says what was heard and which words weren't understood.
Drums can be darker or brighter, shorter or longer (a hi-hat sits at the
noise's D-#: at E-# most of its sound lies above hearing). The instrument
gets a new name, so notes that use another one don't change. The reply
gives its volume a frame at a time and what that means at the song's tempo
("down to half (7) on frame 16 and silent from frame 43, 0.9 of a beat"),
and on the 2A03's pulses the notes its vibrato would click on.

Three kinds are calm leads, the shapes of the quiet tracks in the
reference library, none of which holds a high level from attack to
release (a level held like that presses, however soft the parts around
it). Their times follow the song's tempo, and a volume given scales their
levels:

- **music box**: struck at its volume and gone in 0.9 of a beat (43 frames
  at 75 BPM, half gone by frame 16), 50%, like a music box or a piano.
- **breath**: breathes in from 4 to 15 over 12 frames, sinks to 9 by frame
  22 and holds it, with a faint vibrato half a beat in, and fades over 24
  frames after a release; 50%.
- **falling strike**: struck at 15, falls to 6 over 14 frames and holds it
  there, the faint vibrato half a beat in, a short fade after a release;
  25% (Final Fantasy III's Time Remains).

They're presets too, by the same names. The faint vibrato is theirs: the
period a step flatter for half of each 12 frames, which takes no note of
the 2A03's pulses across a multiple of 256, so it never clicks.

On the noise channel, a kind that isn't a drum makes a volume envelope: a
`swell` rises over half a second and washes out after a release, for waves
and gusts of wind (the note picks the noise's pitch, 0-# deep to F-#
bright). Anything that would move its pitch or make it metallic (vibrato,
an attack's pitch, an arpeggio, a duty) is left out. On a Sunsoft 5B
channel, `noise_pitch` (0-31, 31 the brightest hiss, or lowest, low, mid,
high, highest) makes a sound of the chip's noise at that pitch, as its own
noise sounds are made: alone (`noise` `only`, the default with a pitch: a
drum or a texture like a rain's wash, the notes' pitch not heard) or over
the square at the note's pitch (`with square`); words work too ("a soft
noise at pitch 8 that swells in"). The 5B's three channels share one noise
pitch, so two noise sounds playing at once both take the one set last. A
pulse sound played on
the VRC6 sawtooth overdrives it (the odd values of its duty sequence add 32
to the sawtooth's volume), so a sound asked for there by name plays its
sawtooth version when the song has one ("Horn (saw)", made by
`design_instrument` or a preset for that channel), and Claude says so
otherwise.

The articulations the studied NES soundtracks are made of are there too:
an attack that blooms from about two thirds of its peak in two frames, a
duty click (each note's first frame at another pulse width), a decay in
steps, held notes struck again every so many beats, a soft echo after the
note, a quick pitch flick, and a bass that thumps in from a fourth above.
The kinds "bloom lead", "stepped lead", "click pluck", "echo pluck" and
"thump bass" start from them. The bloom lead blooms and settles to 12, and
the sound library's bloom lead (`add_instrument`, the Sound browser) is the
same sound, a frame at a time, with the library's soft vibrato that never
clicks on the 2A03's pulses; `restrike_beats` 0.5 strikes it again every
eighth, as the classic action leads were (the built-in guide's action and
Konami topics say so). A held note struck again dips over two frames
to about half its level (at most 5 under) and strikes a little over it, on
the beat's grid from the note's start; a sound that dies away (a pluck, a
music box) is struck again from its peak until a release. Drums and the
triangle can't be struck again that way, and the reply says so.

A sound gone within four frames (a pluck with a short decay and no
sustain) is for echoes, stabs and drums, and the reply says so: a tune's
quick notes on it come out as blips (see [Notes that vanish](#notes-that-vanish)).

## Effects and techniques

Claude knows FamiTracker 0.4.6's effects, and only those: `effects` says
what each one does, which channels play it, whether it stays on until turned
off with `00` and what it's good for, and how the classic techniques are
made: an echo channel, a chorus with `Pxx` (P81 on a doubled part), vibrato
that comes in after a beat, swing with `Gxx` on the off-beats, staccato with
`Sxx`, fades and swells with `Axy`, tremolo, the 2A03 pulses' hardware sweep
(`Hxy`, `Ixy`), duty changes with `Vxx`, slides and grace notes, chords as
arpeggios, a kick on the triangle, DPCM tricks and noise drums.

When Claude writes notes, an effect FamiTracker 0.4.6 doesn't have (`Lxx`,
`Mxy`, `Oxx` and `Txy` come from later FamiTrackers; `5xy`, `8xx` and the
like from MOD and XM) or one the channel can't play (a sweep on a VRC6
pulse, tremolo on the triangle, `Vxx` beyond what a channel takes, `Gxx`
longer than a row) is refused with what to use instead, so nothing silently
does nothing. On the FDS channel, `Hxx`, `Ixx` and `Jxx` are its modulator.

## The glossary

FamiTracker has a glossary for people new to music (**Help > Glossary**,
its toolbar button at the end of the toolbar, or Ctrl+Shift+G): about 150
terms in plain words, in four groups. Making music: notes, octaves,
scales, keys, chords and what they do in a key (home, away, tension and
color, as the piano roll's chord strip shows them), melody, phrases,
rhythm, bars, tempo, swing, the drum kit, the song's parts (intro, A, B,
bridge, climax, break, outro, verse, chorus, in the words Module > Song
shape and the sections use), the loop, layers, echo, panning and the mix. Sounds: timbre, envelopes,
vibrato, slides, the pulse width, each channel and each chip. The
tracker: rows, patterns, frames, speed and tempo, instruments and their
sequences, the volume and effect columns, the piano roll, sections and
clips. And every effect. Each entry has its other names (search finds
them), two to four sentences, the terms to see too, and most a **Hear
it** button that plays a short example on the song's channels: a major
chord then a minor one, straight eighths then swung ones, each pulse
width, a vibrato coming in. An example for a chip the song doesn't have
says how to hear that chip instead.

The window stays open beside the song without taking the keys: notes go
on typing in the pattern editor or the piano roll until you click it.
Esc closes it, and it opens again where it was. In the pattern editor,
rest the mouse on an effect to read what its numbers do ("447 Vibrato:
the pitch wobbles, speed 4, depth 7"); the status bar says the same for
the effect under the cursor, F1 opens its entry and Ctrl+Shift+H (Help >
Hear the effect at the cursor) plays the note plain and then with the
effect. A Vxx under an instrument with a duty sequence changes nothing,
and its tip says so (Hear it then plays it on a plain sound). The song settings' labels (Speed, Tempo or BPM, Rows, Frames,
Step, the row highlights, Octave) explain themselves the same way and open
the glossary when clicked, and so do the instrument editor's sequence
names, the mixer's Soft, Space and pan, and the piano roll's Key, Snap to
key, Chords and Groove (F1 opens their entries).

Claude and local models get the same words with `explain`: ask for a term
by any of its names (`{"term": "home note"}`, `"duty"`, `"4xy"`, or an
effect as written, `"447"`, which adds what its numbers do), or for the
list (`{"category": "effects"}`). Other words get the nearest entry and
the others that mention them. It reads no song and changes nothing, and
it's in the small set too.

## Exercises

**Help > Exercises** has its exercises for people new to music in groups.
**First steps**, on top, are eleven of a minute or two each for someone
who has never made music, each teaching one word in the piano roll with a
sound at once: your first note (`first-note`), high and low (`high-low`),
long and short (`long-short`), a steady beat (`steady-beat`), loud and
soft (`loud-soft`, with the piano roll's Effects > Volume), up the stairs
(`stairs`: five notes up the key's lighter lanes and back down), a rhythm
to clap (`rhythm`), copy and repeat (`copy-repeat`), a different sound
(`new-sound`, from the Sound browser), two parts at once (`two-parts`: a
low note on the triangle under a tune) and your first tiny tune
(`tiny-tune`: four notes up and four down, back to where it started),
which leads into the next group. Each passes any reasonable answer and
says what's off in the piano roll's own words ("Your notes are all short
so far: the longest, your first note (bar 1), lasts 2 beats. Make it long:
drag its right edge to the right until it reaches the next bar line").
**Next steps** are ten, a few minutes each, from a tune's last note to a
first small song: the home note, four bars over chords, strong notes on the beat, a phrase that
breathes, a bass line (the chords' roots, then two notes a bar), a beat
with the drum grid, major and minor, a hook that comes back, the song's
shape, and a first small song made with the What next panel. Each opens a
short song made for it (the last two a new song) with a note that gives
the task, a **Hint**, **Hear it** (a good answer in the song for a moment
while its bars play, then your own notes back) and **Check**. Check says
what's right already and the one thing to change next ("Your tune ends on
D (bar 3), which leaves it hanging, as if it wants to go on. End it on C,
the home note"), with the glossary's words in it linked to their entries
and **Play it** for the bars in question. Passing says what it taught and
offers the next exercise, and FamiTracker remembers which are done.

Claude can teach with them too: `check_exercise` checks one on the
selected track the same way, in the same words (`{"exercise":
"home-note"}`; the bass line's two checks are `bass-roots` and
`bass-two`, and `bass` gives the first of them that doesn't pass yet).
`format` `json` gives the verdict as data, the glossary's words marked as
`[[words|id]]`. Without an exercise it lists them. It reads the song and
changes nothing; it isn't in the small set. Its list names each group
with its exercises (Making it your own, a heading for exercises still to
come, has none yet).

## References: learning from NES soundtracks

Give Claude NSF files you have (game music, ripped as `.nsf` or `.nsfe`;
put the `.m3u` playlist of the same name next to it, and the tracks get
their names) and ask it to study them, like "study the Mega Man 2 NSF in
my Music folder". `study_nsf` plays each track that's music (jingles and
sound effects are told apart), works out its notes and writes down how
it's made: the tempo and form, the key and the chords bar by bar, what each
channel does (its role, register, rhythm as a grid of its commonest bar,
intervals, and for the bass its figure), the drums' grooves, the tricks
between channels (an echo a row behind, harmony in thirds, doubling, notes
traded) and the techniques in the sounds (duty clicks, attacks that bloom,
echoes in the envelope, decays in steps, scoops, pitch flicks, vibrato
with its delay, triangle thumps), with the main sounds' envelopes.

The loop is the stretch the track keeps on repeating, from where you can
first hear it repeat: a passage played twice inside a piece that then
moves on isn't a loop, a first time through that differs only in a part
you can barely hear (a quiet pulse's tails) loops from the start, and
drums that run on a cycle of their own beside the tune don't hide the
tune's loop. A track that falls silent long before its playlist says is
studied for what it plays: a sting or a sound effect of the seconds it
sounds. A sampled bass on the DPCM channel (Sunsoft's sound: one wave
played at many rates) is studied as the bass, with its range and how it
moves, and `import_nsf` puts its samples on the keys of the notes they
sound, as FamiTracker's DPCM tab does. The key comes from the chords, the
bass and where the phrases come to rest; music whose chords keep drifting
gets "no clear key", and a tonic that comes both ways "C (major and
minor)". Studying a track again keeps your tags and notes.

Each study is kept as a reference in a library of yours, the `References`
folder next to FamiTracker.exe (a JSON file each), for every song and every
session, so it moves with FamiTracker. References kept in
`Documents\FamiTracker references`, where the library used to be, move into
it by themselves, and so does a `FamiTracker references` folder unpacked next
to FamiTracker.exe. Where FamiTracker's folder can't be written (under
Program Files), the library stays in Documents. When you ask for music "like Bucky O'Hare", "in a Mega Man
style" or "lean on the references", Claude searches the library
(`references`) and plans from what it finds, and `reference_sound` brings
in a reference's instrument exactly. Only its release can differ: a studied
sound whose release ended above 0 held that level after a release (`===`),
droning on until the next note (535 of one library's 823 sounds), so its
instrument fades out from there instead, halving a frame, in the sound
browser and `find_sound` too, and the reply says so. The melodies aren't kept: Claude
takes the craft (grooves, bass figures, harmonic color, how the channels
share the parts, the sounds) and writes its own tunes and progressions.
Built in, without any NSF, is a guide to the classic NES styles from
studies of Konami's (Bucky O'Hare, TMNT II, Castlevania, Castlevania III,
Super C), Capcom's (Mega Man 2-5, Little Nemo, Strider), Nintendo's (Super
Mario Bros. 2 and 3, The Legend of Zelda, Kid Icarus on the Famicom Disk
System, Gyromite, Punch-Out!!, Dr. Mario), Square's (Final Fantasy III),
Sunsoft's (Journey to Silius) and Technos's (Nintendo World Cup)
soundtracks. Its `dark` topic is the craft of dungeons, caves, crypts and
the underworld in all of them (low, narrow registers, semitones and
tritones, pedals and ostinatos, music without drums, quiet echoes), and
`melancholy` that of sad scenes, laments and night. Its
`soundtrack` topic is about a game's music as a whole, from composing a
30-track soundtrack with these tools: a catalogue of places, a leitmotif
that comes back changed, tempos whose rows all last the same, a loudness
for each kind of place, and forms for towns, dungeons, bosses and the
credits.

You can tag references and keep notes with them ("the user loves this
bass"), and `import_nsf` (or File > Import NSF) opens tracks as a module to
look at the notes themselves. `compose_track` looks up a composer, a game
or a reference named in its idea or in the soundtrack notes by itself, so
"a save room in the style of <a composer whose modules you studied>" plans
from those studies. A composer is found by the name the modules give as
their author.

### Sounds from twelve soundtracks

FamiTracker carries the studied sounds of twelve soundtracks, so they're
there without any NSF or library: Castlevania, Castlevania 3, Dr. Mario,
Final Fantasy 3, Journey to Silius, Nintendo World Cup, Partena no Kagami
(Kid Icarus on the Famicom Disk System), Punch-Out!!, Robot Block, Strider
Hiryu, Super C (Super Contra) and The Legend of Zelda: 615 sounds of 152
tracks, each sound once with the tracks it plays in, named as the browser
names a study's sounds (the track's title as far as a parenthesis, or its
first words when it's long, and the part: "Vampire Killer lead" for Vampire
Killer (Courtyard), "Beginning bass" for Beginning (Block-1); a number that
ends a title stays, "The Everlasting 2"). Only the sounds come with
FamiTracker (their envelopes, duties,
waves and modulators), none of the studies' other notes. The sound browser
and `find_sound` list them game by game, and a sound of theirs that your
library has too shows once, as theirs; `reference_sound` adds one by its
track's id (`reference_sound reference "castlevania-02" sound 3`), library
or not. `Source/Api/tools/game_sounds.py` writes them
(`Source/Api/GameSoundsData.h`) from a folder of studies, taking only those
games.

### Modules as references

Music made in the PC trackers works too: give Claude a MOD, S3M, XM or IT
file of yours, like "use song.xm in my Music folder as a reference for the
boss theme". `study_module` reads only the module's patterns (its samples
aren't read, and nothing of it goes into your song) and keeps a study in the
same library: the tempo and form, the key and chords, each channel's role,
register, rhythm and instruments by name, the bass's figure, the drums'
grooves (the drums are told by their instruments' names, or by playing one
note over and over; an instrument with a sample a key splits into its
drums), the tricks between channels, and the techniques written into the
patterns (arpeggios, vibrato, portamento, volume slides). A module has more
channels than the NES, so Claude boils it down: the lead on a pulse or the
sawtooth, harmony, pads and chords on the other pulse or as arpeggios, the
bass on the triangle, the drums on the noise and the DPCM. The tunes stay
the module's own: Claude writes new ones that sound like they belong to it.

FamiTracker modules (`.ftm`, from 0.2 to 0.4.6) are studied a track at a
time from their own patterns and instruments, like "study
C:\Users\Name\Music\song.ftm": besides what any study finds, the speed and
tempo as written (and a swing, or a lilt between two speeds, from speeds
that alternate), echoes that run only in some sections, chords played by
arpeggio instruments, notes delayed with `Gxx`, duty changes with `Vxx`,
detune with `Pxx`, the DPCM level written with `Zxx` and each channel's
volumes. Their sounds are kept with their sequences as written, so
`reference_sound` gives a song one of them.

MIDI files (`.mid`, `.midi`, `.rmi`) are studied the same way, like "study
the town theme in C:\Users\Name\Music\town.mid". Their notes go on a grid
of rows (4 a beat, or 6, 8 or 12 for triplets and thirty-seconds), and
their bars follow the file's meter, so a waltz is counted in 3/4 and a jig
in 6/8 (two beats of three eighths). Each part becomes channels of one note
at a time: a piano's chords spread over several, the highest note on the
first, so a melody stays on one channel; the drums (MIDI channel 10) go on
a channel for each kind (kick, snare, hi-hat, cymbal, tom, percussion),
named after General MIDI's drums. The channels are named after the file's
tracks, or after their General MIDI instruments, and a track called
"Melody", "Lead" or "Soprano" is taken as the lead. The tempo is the one
the file keeps longest, and a loop point (controller 111 as RPG Maker
writes it, EMIDI's loop controllers, or `loopStart`/`loopEnd` markers)
makes it loop. Grace notes are left out. A MIDI study is tagged `midi`, and
with its meter when that isn't 4/4.

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

## Day and night versions

Ask for a night version and Claude adds it as a new track in the same file,
named like "Town (night)", next to the day version, which stays as it was.
The two share the module's instruments, and FamiTracker shows the new track
afterwards (switch with the track box in the toolbar).

Night is much more than slower. The night version is, unless you ask for
something else:

- lower: the parts that sat high (from C-4 up, not the bass) an octave down,
- simpler: the melody's fast runs thinned to their outline (the short notes
  between the eighths left out, the notes before ringing on),
- legato: notes held into the next (no note cuts; a halt just before a note
  goes, the others soften to releases), with even dynamics, no accents,
- with chords held as a pad that swells in (a chord struck again in its bar
  rings on instead; ask for "richer chords" to turn triads into sevenths),
- with the bass in long notes (the notes on the beats, held),
- with the drums a soft pulse on each bar's first beat (sampled drums on the
  DPCM channel thin out the same way), or none if you ask,
- with a soft lead that swells in for the melody and a warm pad for
  arpeggio chords, a slower and wider vibrato starting later in the note,
  and a longer, softer echo (no second echo when the song already has one),
- 20% slower (tempo commands in the patterns slow down too) and quieter
  (every channel's volume at 75%).

Claude says what it changed. Say what you want different, in words: "keep
the drums", "only 10% slower", "no echo", "keep the melody as it is", "in
dorian". Claude can also change the night version afterwards with the other
tools, since it's the selected track.

A **tense version** (for a battle, danger, a boss or a chase) is, unless you
ask for something else:

- 15% faster,
- in minor when the song is in major (a song in minor keeps its mode),
- with a bright lead for the melody,
- with a driving bass: each bass note struck again on every eighth note,
  cut short before the next (notes that slide stay as they are),
- and with busier drums: soft hi-hats between the beats the drums leave
  empty.

Other versions (rain, underwater, a dungeon) change only what you ask for.

Adding a track can't be undone with Ctrl+Z in FamiTracker; remove a version
you don't want in **Module > Module properties**.

## Jingles

Ask for a jingle ("make a level-up jingle", "a jingle for when a quest is
done") and Claude adds it as a new track: a short tune with a lead, a
harmony line below it, a bass and drums. It's in the song's key note, near
its melody's register and with its sounds (the lead's instrument, the bass's),
and it ends with a halt (`C00`), so the export for games saves it as a sound
that plays once. Those come from the music: when the selected track is a
sound effect (as right after `make_sound_effect`, which selects its new
track), the jingle takes the key and the sounds of the nearest music track,
never an effect's instruments.

| Kind | For | Length |
|---|---|---|
| level up | a level up, a new skill, a power-up | 3.2 s |
| victory | a quest completed, a battle won, a stage cleared | 3.6 s |
| item | an item, a treasure or a key found (on a bell) | 1.7 s |
| new quest | a new quest, a message, a notification (two bell chimes) | 1.5 s |
| discovery | a secret, a new area, a solved puzzle | 3.2 s |
| rest | a rest at an inn, a save, healing (slow and soft) | 6.7 s |
| defeat | a lost battle, a game over (minor) | 6.3 s |
| danger | an alarm, a trap, a boss appearing (minor, unresolved) | 2.9 s |

Defeat and danger are in minor and the others in major, on the song's key
note (or the one you name). You can ask for another tempo, other
instruments, channels, or no drums or harmony. Since the jingle is the
selected track afterwards, Claude can change its notes like any other part.

## Sound effects

Ask for a sound effect ("a jump sound", "three coin sounds to choose from")
and Claude adds it as a new track, made the NES's way: a pulse, the triangle
or the noise channel moving its volume, pitch and duty 60 times a second. The
track plays at speed 1, a row a tick, and halts (`C00`) once the sound has
died away, so the export for games saves it as a sound that plays once:
`audio_play_sound(snd_sfx_jump, 0, false)`. Each part is an instrument whose
sequences shape the sound, named after the track ("SFX Jump"), so you can
change it in the instrument editor.

Every sound that plays once **rings out to silence**: a part that would stop
while still loud dies away first (its volume halving a tick, down to 1; the
triangle, which has no volume control on the NES, stops where it ends), and
the track halts five ticks (0.08 s) after the last tick that sounds, so no
sound is cut while it plays and the game's file of it ends quiet. The coin
is a glint of our own (A, then the F# a sixth above ringing out with a
shimmering duty), not a famous game's coin.

| Kind | For |
|---|---|
| jump, land, fall | jumping, landing, falling into a pit |
| coin, pickup, power-up, chest | coins and gems, items, upgrades, opening a chest |
| hurt, stomp, laser, explosion | taking damage, landing on an enemy, shooting ("pew"), a blast ("kssh") |
| bounce, warp, whoosh, splash, door, footstep, break | a spring ("boing"), teleporting, dashing, water, doors, steps, breaking a pot |
| select, confirm, cancel, error, text | menus, OK, back, something that can't be done, dialogue letters |
| message, heal | chat messages and notifications, healing |
| wind, rain, fire | ambience: a heavy wind blowing in gusts, rain pattering, a fire crackling (these loop) |
| thunder, birds, wings | a crack of thunder rolling away, two birds chirping, a bird flying off ("bird flying away") |
| engine, alarm, heartbeat | an engine idling, a siren, a heartbeat for low health (these loop) |

Every kind has variants (variant 2, 3, ... are a little higher or lower,
shorter or longer, some with another duty), and Claude can move a sound by
semitones, stretch it, or make it softer. A sound that plays once lasts at
most 4.2 seconds; a loop as long as you ask (below).

**Ambience loops** instead of halting: its track ends with a jump back to
its start (`B00`), and what reaches its end (a gust, a flutter) goes on into
its start, so the export for games saves an OGG that loops without a seam
(stop it with `audio_stop_sound`). Any sound can loop if you ask, and a kind
that loops can play once instead.

**Long loops for ambience.** Rain or wind that repeats every few seconds
shows; ask for a loop as long as you like (`seconds`, 30 or more for
ambience) and the kinds with random parts (wind's gusts, rain's drops, a
fire's crackle) are drawn for all of it, while the engine, the alarm and the
heartbeat play their own loop over and over. A loop lasts as long as the
track's frames hold, a row a tick (136 seconds at 64 rows a frame). A layer
longer than 4.2 seconds, the most an instrument's sequences hold, plays as
pieces in turn, an instrument each with a note where it starts ("SFX Rain",
"SFX Rain (part 2)", ...), pieces alike sharing one; the chip plays on across
them, so there's no seam. A module holds 64 instruments and 32 seconds of
rain takes 16, so Claude says when there's no room: keep sound effects in a
module of their own.

**Sounds described in words.** For anything else ("a bird flying away", "a
heavy wind through a ruin", "a robot powering down"), Claude builds the
sound from layers: a part each on a pulse, the triangle or the noise
channel, with when it sounds, its pitch and how it slides, its loudness and
how it moves (a hit, a fade, a swell), repeats (chirps, flaps, beeps) and
the textures of nature: gusts, a flutter, crackle, drops. Layers that don't
sound at once share a channel. The reply lists the layers in words, so you
can ask for one to change ("the flaps slower", "the chirp higher"), and a
variant of a described sound also draws its gusts, crackle and drops anew.

Claude can also turn **a recording of your voice** into a sound effect: give
it the path of a WAV file ("C:\Users\Me\Documents\pew.wav"). Where your
voice has a pitch, a pulse follows it, sliding between notes, moved by whole
octaves so it sits where NES sound effects do; the breathy and hissy rest
plays on the noise channel, brighter for "sss" than for a breath. Silence
before and after is left out, and a sound can last up to 4.2 seconds.
FamiTracker can do this without Claude, too: Edit > Sound effect from your
voice records the sound, plays it as the NES would and adds it with this
same tool (see `RECORDER.md`). Its takes stay in the `Recordings` folder
next to FamiTracker.exe, so you can ask Claude for another version of one
("the last voice take, an octave lower": the recording `"latest"` is the
newest take).

Keep sound effects in a module of their own ("Sound effects.ftm"), so your
music file stays clean; Claude works on whichever module is open.

**Short sounds at a loudness.** EBU R128 measures loudness in blocks of 400
ms, which a sound shorter than that doesn't fill (a menu blip, a pickup, a
whoosh), so `export_for_game` with `loudness` measures such a sound over its
whole length instead, without the gating, and brings it to the loudness
asked like any other; the reply says when it measured one that way.
`measure_mix` on a sound effect measures it whole the same way, with each of
its channels alone, and `review_arrangement` gives a sound effect a short
check of its own (its layers, whether the halt cuts it while it sounds,
notes that click) instead of a score.

## Music for your game: looping OGG files

**File > Export for games (OGG)...** saves the selected track, or all of them
(day and night versions together), as OGG files next to the song, named
after the tracks: "Town.ogg", "Town (night).ogg". Ask Claude for it too
("export the music for my game").

Each file plays the song's intro once (the bars before where it loops), then
the loop, cut so it repeats without a seam:

- FamiTracker renders the song past its second time through the loop and
  notes the exact sample where every row starts.
- If the loop starts the same way both times (no note, effect, instrument or
  volume from the intro hanging over), the file is the intro and the loop
  once. Otherwise it holds the loop twice and loops the second time, which
  starts the way every repeat does. `check_loop` tells you which, and why;
  `fix_loop` makes the loop start the same way both times where it can.
- The loop points move together by up to 2 ms to where the waves meet, so
  the jump doesn't click; the loop keeps its exact length.
- A song that sets the DPCM level on its first row (as `mix` does) used to
  step the DMC's output up from silence there, a pop that the repeats
  don't have. FamiTracker now starts such a song at that level, in
  playback and in the files. The file still fades in over its first 40 ms,
  before its level is measured, and a song that loops as a whole still
  repeats from its second time through, so the fade stays out of the loop.
- A song or a sound effect played or exported after another used to start
  with a thump: the triangle holds the step its wave stopped on, and
  FamiTracker counted that step again with the next song's first sound, as
  loud under a soft noise at volume 1 as under one at 15. Each song now
  starts from the levels the last one left, so its first sound plays at its
  own level.

GameMaker (2023.2 or newer) loops a sound between two points. The dialog and
Claude's reply give the code, for example:

```
audio_sound_loop_start(snd_town_night, 12.800000);
audio_sound_loop_end(snd_town_night, 38.400000);
audio_play_sound(snd_town_night, 0, true);
```

Add each file as a sound with that name (snd_ and the file name). The loop
points are also in the files as `LOOPSTART`, `LOOPLENGTH` and `LOOPEND`
comments (in samples), which RPG Maker and other engines read
(`FAMITRACKER_PRINT` and `FAMITRACKER_LOUDNESS` say what the file was made
from, for `what_next`). A song that
ends with a halt (`Cxx`) plays once, like a jingle.

**Loudness** picks how loud the files come out, and is remembered:

- "As loud as it goes (peak at -1 dB)", the default: each track is brought
  up until its loudest peak is at -1 dBFS.
- "Calm place (-22 LUFS)", "Most tracks (-20 LUFS)", "Action and bosses
  (-19 LUFS)", or "Other..." with a number from -40 to -6: every track
  exported (with All tracks, all of them) is brought to that loudness, as
  EBU R128 measures its loop over both channels (the `loudness` of
  `export_for_game`).

Tracks at the same loudness sound equally loud in the game, one after
another; tracks brought up to their loudest peak don't (a quiet track with
one loud hit stays quiet). At a loudness, a track's loudest peak still stops
at -1 dBFS: a track it holds back comes out a little quieter than the
others, and the result says so (a lower loudness for all of them, or that
peak softened, makes them match).

The song has to be saved, since the files go next to it; files of the same
names are replaced. "Normal" quality suits chiptunes; "Best" makes bigger
files. The files play the song as FamiTracker does, with the mixer's faders,
pans, Soft and Space (View > Mixer). Every channel is in the files, also the ones muted in the pattern
editor; they're muted again afterwards.

A track whose notes click on the 2A03's pulses (see "Clicks in held notes
on the 2A03's pulses") is exported as it is, and the reply says so:
`polish` with the touch `clicks` nudges those notes, then export again.

### The last steps in one call

`finish_song` does the last steps the recipes ask for, in order, each on the
song as the one before left it:

1. `polish` with the kind's touches, the ones `what_next`'s polish step
   gives: polish's calm style on a calm track, its lively style on a lively
   stage (the default touches, accents and all), and polish's own for
   action and the general recipe.
2. `mix` in the kind's style: `calm`, `lively`, or `soft` for action and the
   general recipe. When the mixer's faders do the mix's work (the melody's 4
   dB down or more, or a part's 9 dB or more over it, as the review's finding
   has it) it mixes with `faders` `reset`, so the balance goes into the
   notes, as NSF files play it; `faders` `keep` leaves them, when you set
   them, and `reset` sets every one back. Trims of a few dB stay, counted in
   the mix.
3. `check_loop`, and `fix_loop` where the seam needs it.
4. `listen`: the song as it now sounds, the mix and each part alone. It's
   kept as a `listen` call's is, so `what_next` takes the song as heard and
   gives its high findings' fixes as its next steps; the reply lists them
   with their calls (`Heard:` lines), and says to export again once they're
   fixed. A listen of the song as it is already (the steps changed nothing
   since one) isn't made again. A cue and a sound effect aren't listened to.
5. `export_for_game` at the kind's loudness: -22 LUFS calm, -20 lively and
   the general recipe, -19 action; a cue -20, a sound effect -18.

The kind is `kind` (calm, lively, action or general), or calm when the
track's name says so ("Town (night)"), else the general recipe's. A cue (a
track that halts within half a minute) and a sound effect (a row a tick)
keep their notes as written and only get their file. The melody is the one
`melody_channel` names, or guessed once before the steps (so a step's
changes can't move the guess), and the reply names it. `skip` leaves steps
out (`polish`, `faders`, `mix`, `loop`, `listen`, `export`), and `loudness` and
`quality` go to the export. It needs direct changes (with `skip` `polish`,
`mix` and `loop` it changes nothing: it listens and exports), and the export a saved song: until then
the reply gives the `export_for_game` call to make once it's saved.

The song's changes are one Ctrl+Z, and the reply is short, a line a step and
then what to listen for:

```
Finished "Town" as a lively stage, the melody on VRC6 Pulse 1 (guessed; melody_channel names another):
- Polish: accents on VRC6 Pulse 1, VRC6 Sawtooth.
- Faders: back to 0 dB (2A03 Noise +6.0 dB, VRC6 Pulse 1 -4.5 dB), which did the mix's work: ...
- Mix (lively): as heard, the melody (VRC6 Pulse 1) 3.0 dB over the chords, the bass 1.0 under and the drums 9.6 under; the band together 1.4 dB over the melody.
- Loop: bars 5-16 (19.2 s), after an intro of 6.4 s; fix_loop wrote 400 on VRC6 Pulse 1 at bar 5 beat 1.
- Listen: nothing sounds wrong as FamiTracker plays it (the held notes in tune, no warble, clicks or drones, a clean mix).
- Export: Town.ogg: 25.6 s, 123 KB, its loop at -20.0 LUFS (...), the first 6.4 s once, then the loop from 6.4 s to the end.
- GameMaker: audio_sound_loop_start(snd_town, 6.398000); audio_sound_loop_end(snd_town, 25.598000); ...
One Ctrl+Z in FamiTracker undoes the song's changes together (the exported file stays).
Listen for: the band carrying the tune, the lead's accents on the beats, and the climax lifting; the seam
going round (play {"from_bar":15} plays the end into the loop; stop ends it).
```

The result is what the calls one after the other would give (`polish`, `mix`
with the same melody, `fix_loop`, `listen`, `export_for_game`): the same notes, mixer
and file, with the print of the song `what_next` reads. The numbers are the
tools' own (polish's and mix's styles), so a change to a style reaches the
finish too. `what_next` gives it
for the last steps, from the first of them that's due, with the kind and
the melody it follows, and it's on the small set.

## Safety

- FamiTracker only listens on 127.0.0.1, so other computers can't reach it,
  unless you allow other computers on your network (then only home networks
  and Tailscale; see [Over the network, safely](#over-the-network-safely)).
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
- **Another computer can't connect** (it times out, or
  `famitracker-mcp-linux` says "didn't answer"): Windows' firewall keeps
  FamiTracker out. In **Windows Security > Firewall & network protection >
  Allow an app through firewall**, tick FamiTracker for Private networks
  (and Public, if Windows thinks your home network is public; or set it to
  Private in its network settings). Check the address too: both computers
  on the same network, or both in your Tailscale.
- **"Connection refused" from another computer**: **Allow connections from
  other computers on my network** isn't ticked, the connection is off, or
  the port isn't the one in the address.
- **"FamiTracker only answers computers on a home network"**: the other
  computer's address isn't a home network's or Tailscale's, as through some
  VPNs. Connect through the home network or Tailscale.
- **"FamiTracker only answers requests to this computer's addresses or
  name"**: the client used a name FamiTracker doesn't know as its own (a
  name of your own in a hosts file, say). Use the address from the dialog.

## Not yet

- Recording ideas from a phone page into the clips.

## For developers

- `Source/Api/` is plain C++14 without MFC, built and tested on Linux:
  `Json` (reader and writer), `Http` (request framing), `Mcp` (the protocol:
  per-request metadata of 2026-07-28 and the initialize handshake of
  2025-03-26 to 2025-11-25, Host, Origin and token checks), `Music` (cells,
  note names, bars and beats, keys and modes, writing a part into a range of
  rows, and following the song through its jumps, skips and halts),
  `Presets` (also the VRC7's patches, the 5B's own sounds and the presets
  as the 5B plays them, and `DesignPreset`: a sound described in words as
  sequences), `Wavetables` (the N163's wave sounds), `FdsSounds`
  (the FDS's sounds: their waves, modulator tables and sequences), `Chords` (chords read from names and degrees,
  progressions laid out on bars, colors, and the chords heard in notes),
  `Sections` (a track's named runs of frames, kept with their frames as
  frames go in, come out, move or are copied, and the `BF SECTIONS` block,
  read with every read checked; the frame editor, the piano roll's ruler
  and `CFrameAction` use it too), `ChannelMix` (the mixer window's faders,
  starting pans, Soft and Space by channel ID, the space they send to, and
  the `BF MIXER` block, version 2 only when Soft, Space or the space is
  used, read the same way),
  `Arrange` (chord parts in styles, bass lines, drum grooves with fills, a
  harmony voice, a counter-melody), `Picture` and `Png` (the piano-roll
  picture, a palette PNG with its own small deflate), `Jingles` (the
  jingles' scores, in semitones from the key note and sixteenth notes),
  `SoundEffects` (the sound effect kinds, built a tick at a time or from
  layers, and `DesignSoundEffect`, which builds sounds described in words
  from layers: slides, envelopes, repeats, gusts, flutters, crackle and
  drops), and
  `Tools` (the tools, against the `SongHost` interface in `Song.h`, the
  server's instructions and the recipes served as MCP prompts), and
  `ProblemList` (`check_song`'s JSON read back for FamiTracker's problems
  panel, and the marks the pattern editor and the piano roll draw), and
  `WhatNext` (`what_next`'s portable pieces: a dry run of a tool that keeps
  its changes back and can read the song as they would leave it, the kinds'
  choices, a new song's chords and the phrase offered for a melody; the tool
  is `WhatNextTool` in `Tools.cpp`). Tool
  results can carry an image after their text.
- `Source/Transcribe/SoundEffect.cpp`: sound effects as layers of per-tick
  values and as FamiTracker instruments (each tick an absolute arpeggio picks
  the nearest note and the pitch sequence moves the period to the pitch
  between notes; the noise uses a fixed arpeggio), `RenderEffect` (the 2A03
  played through those sequences, for the tests) and `VoiceToEffect` (a
  recording's loudness, pitch and zero crossings, tick by tick), and
  `VoiceTake` (when a take from the microphone is over).
  ```
  cmake -S Source/Api -B build/api
  cmake --build build/api
  ctest --test-dir build/api --output-on-failure
  ```
  The tests run every tool against a song in memory and the protocol with
  the requests real clients send. The protocol was also checked against the
  official MCP TypeScript SDK clients (1.x, and 2.x in both negotiation
  modes).
- `Source/Api/GameAudio.cpp`, `Ogg.cpp` and `Music.cpp` (`TimeRows`): the
  export for games. `TimeRows` counts the ticks of every row as `CSoundGen`
  plays them; `CutForGame` finds the loop in a render from the rows' sample
  positions (`CSoundGen::GetRenderMarks`, recorded while rendering); `Ogg`
  encodes with libvorbis (`Source/libvorbis`, `Source/libogg`, BSD licensed,
  as released). The tests render with a stand-in synthesizer and decode the
  files again.
- `Source/McpSocket.cpp`: the listener thread (Winsock, no MFC). It frames
  HTTP requests and hands each to the UI thread with `SendMessageTimeout`,
  each with its client's address (`HttpRequest::Peer`). With other computers
  allowed it binds every IPv4 address and closes connections from addresses
  outside home networks at once; `ListNetworkAddresses` gives the dialog this
  computer's addresses (`GetAdaptersAddresses`).
- `Source/Api/Network.cpp`: who may connect from the network (`MayConnect`:
  loopback, 10/8, 172.16/12, 192.168/16, 169.254/16, 100.64/10, or one of
  this computer's own addresses), the Host headers that name this computer
  (`NamesThisComputer`), the names files may have in the Inbox and the file
  route (`IsPlainFileName`, `IsInboxFile`, `IsExportFile`), links and base64.
  `HandleMcp` routes `/mcp`, `/mcp/core` (the tools of `ToolSet::CoreTools`
  with `ToolSet::CoreInstructions`, no prompts), `GET /files/<name>` and
  `PUT /inbox/<name>`; `ToolSet::Call` takes a `Caller` whose `FilesUrl`
  makes `export_for_game` link its files.
- `Source/Api/Bridge.cpp` and `Source/Api/tools/famitracker-mcp.cpp`: the
  connection for Claude Code in WSL, an MCP server over stdio. Each JSON-RPC
  line becomes the HTTP request Claude Code would send (with the protocol
  headers the endpoint checks) and each answer a line; an answer refused
  before it was read (a wrong access code) still goes to its request.
  `BridgeSocket.cpp` sends it to 127.0.0.1 (Winsock, or BSD sockets for the
  tests). On Windows it reads the port and the access code from
  `HKEY_CURRENT_USER\Software\FamiTracker\Claude Code` for every message;
  `--port` and `--token` override them. With `--host` (an address or a
  name) it connects to FamiTracker on another computer instead, within 10
  seconds, and `--core` picks `/mcp/core`. Its sources build alone as the
  `ftbridge` library, and CI links it statically against musl on Alpine for
  the release's `famitracker-mcp-linux`. The tests also run it against the
  endpoint over real sockets, by address and by name.
- `Source/McpConnection.cpp`: `SongHost` over the open document, the
  message-only window that answers on the UI thread, the settings (registry
  section "Claude Code") and the dialog. Song changes go through
  `WriteSongParts` in `InsertRecording.cpp`, one undoable action each, which
  can first put whole frames into the frame list or take them out
  (`insert_bars`, `remove_bars`); Undo puts the frame list back. Paths
  the tools are given (UTF-8) reach the C library in the Windows code page
  (`SongHost::LocalPath`).
- `Source/VoiceEffectDlg.cpp`: Edit > Sound effect from your voice. It
  records with `CMicInput`, previews with `RenderEffect` through
  `PlaySound`, and adds the track through `CVoiceEffectMaker`, which
  `CMcpLink` implements with the `make_sound_effect` tool.
- `Source/Nsf/`: the NSF importer, plain C++14 built and tested on Linux
  (`cmake -S Source/Nsf -B build/nsf`). A 6502 and the sound chips'
  registers (`Cpu6502`, `Chips`), the player that records a track a tick
  at a time and finds its loop (`Player`), the conversion to a FamiTracker
  module (`Convert`: the rows, notes and instruments; `Module`: the text
  format), `Import` (all of it in one call), `Simulate` (FamiTracker's
  playback of a module, to measure how close a conversion is) and `Study`
  (a converted track described for a composer, the survey of which tracks
  are music, and studies of tracks one at a time). `nsf2ftm` runs it from
  a command line. The tests build little NSFs with their own 6502 program
  (`tests/TestNsf.h`, also used by the Api tests).
- `Source/Api/DpcmKit.cpp`: the sampled drums, synthesized (a body that
  falls in pitch, noise for the snare) and encoded as the DMC's delta
  modulation from silence, never stepping past the counter's ends (it holds
  there instead) and back at its start level at the end, so a sample started
  from another level (`Zxx`) walks the same way and ends where it started;
  `SongHost::AddDpcmKit` adds the samples and the instrument that maps them
  to keys, and `RefreshDpcmKit` brings a kit an older version made up to
  date.
- `Source/Api/References.cpp` and `StyleGuide.cpp`: the reference library
  (references as indented JSON, searching them, a sound as a preset) and
  the built-in style guide. The library's files are read and written
  through `SongHost::ListLibrary`, `ReadLibrary`, `SaveLibrary` and
  `DeleteLibrary`; `import_nsf` opens its module with `SongHost::OpenText`,
  which `ImportNsfDlg.cpp` (File > Import NSF) shares. `study_module` reads
  a tracker module with `Source/Nsf/Tracker.cpp` (MOD, S3M, XM and IT
  patterns, followed through their breaks and jumps into a module of one
  track with a channel layout of its own), or a MIDI file with
  `Source/Nsf/MidiFile.cpp` (read into the same kind of song, with its
  meter and channel names), or a FamiTracker module with
  `Source/Nsf/FtmFile.cpp` (a track at a time), and studies it with
  `Study.cpp`.
- `Source/ChipSwitch.cpp`: what `set_expansion` and Module > Module
  properties do when the chips change: the module's channels for a set of
  chips, counting the notes on the channels that go, emptying them in every
  track, moving their `Fxx`, `Bxx`, `Dxx` and `Cxx` to the last channel (a
  free effect column on the row; a frame whose pattern other frames play
  differently gets its own), and `SelectExpansionChip` moving the channels
  that stay to their new columns. `Source/Tests` checks it against the
  stand-in document.
- `Source/Api/PhoneTakes.cpp` and `QrCode.cpp`: the phone page's routes
  (`PhoneTakes::Handle`, which `HandleMcp` calls after its peer and Host
  checks; `PhoneCode`, the page's code, HMAC-SHA-256 of the access code;
  `CheckPhoneWav` and `IsIdeaName`, the checks before anything is kept), the
  takes waiting until the song can take them (`Process`, through the
  `PhoneHost` interface), and the QR encoder (byte mode, versions 1 to 40,
  levels L to H, the mask the penalty rules pick), which `tests/PhoneTests.cpp`
  compares with a reference encoder's codes. On Windows `Source/PhoneLink.cpp`
  is the host (the `Recordings` folder, `GuessRecording` and `InsertRecording`
  with a clip name), `Source/PhoneDlg.cpp` the window with the QR code, and
  `res/phone.html` the page, built in as RCDATA.

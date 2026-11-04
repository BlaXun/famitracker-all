# Songsmith

Songsmith lets your local model compose NES-style songs for the game in FamiTracker on its own, overnight. You
queue songs on a small web page (a name, a few sentences about the map, a kind, a chip). Songsmith works through
them one at a time, in eight fixed phases, the way the wiki's one-shot prompt describes:

| # | Phase | What the model does | What the harness checks after it |
|---|---|---|---|
| 1 | Plan | reads the kind page, one example, Craft moves; writes the plan table | the plan has a table |
| 2 | Start | `set_tempo`, `set_key`, `song_shape` with the kind | sections and a chosen key |
| 3 | Sounds | `design_instrument`, `find_sound` | the sounds are there |
| 4 | Hook and melody | its own hook, `grow_melody`, every section's tune | a melody; `check_song` |
| 5 | Parts | chords and bass, drums, second lines, per section | channels play; `check_song` |
| 6 | Development | the Yardstick and Craft moves: repeats varied, parts changing per section, dynamics | `check_song`, `review_arrangement`, the yardstick |
| 7 | Checks | `check_song`, `listen`, `review_arrangement`, `measure_mix`, the loop | the same, and the mix |
| 8 | Finish | `finish_song` with the kind and flavour, until clean | `check_song`; the OGG exported |

Songsmith itself creates the song (`new_song`, into FamiTracker's Songs folder, never over another song), makes a
backup before every phase (`create_backup`) and saves after it (`save_song`). The model never gets those tools.

Each phase is a short conversation of its own: the model gets the song's brief, a short note of where the song
stands (its plan, each finished phase's summary, `get_song`'s overview), the wiki pages that phase needs, and only
that phase's tools. So the context stays small even over a long night.

**The wiki.** Songsmith reads the FamiTracker wiki fresh for every song and hands each phase the pages its
`AGENTS.md` reading order and `Librarian.md`'s "where to look" name: the kind page, one example of the kind (another
for each version), Craft moves (all of it for the plan, the moves that matter for each later phase), the kind's
section of its hook cookbook for the melody, the Yardstick for development and checks, Loops and seams for the
finish, the flavour's row of Flavours when a flavour is set, and the rows of Verdicts that concern the kind (your
ear wins over any page). The model also has two wiki tools of its own, `wiki_search` and `wiki_read` (a page, or
one section by its heading, verbatim): the librarian's job, without a second model. `raw/` is never offered.

After each song it files back as `AGENTS.md` says: a song log in `Song logs/<song>.md` (from the template's form)
and a line in `log.md`. The verdict box on a song's page files your words into `raw/verdicts/Verdicts verbatim.md`,
a row at the top of `Verdicts.md` and `verdict:` in the song log, with a line in `log.md`. No other page is ever
written.

**Gates.** After each phase Songsmith checks the song itself through FamiTracker: `check_song` (format json) must
have no finding worth fixing (high or medium) that still has a fix; `review_arrangement` must score at least the bar
(8 by default); `measure_mix` must be sane (the loudness in range, no part but the bass well over the melody; on the
full tool set only); the yardstick rows it can count from `get_song` and `read_notes` (parts at once, instruments,
the melody's rhythms, how often its rhythm repeats, the hook's return, its register, the bass's rhythms, channels
changing per 8 bars, the band in the last two bars, the noise in a soft song) must be under in at most 3 rows. When a
gate fails, the model gets the findings with their calls and tries again (2 times by default); after that Songsmith
makes `check_song`'s fixes itself, and then goes on with a note in the report rather than stalling.

**Reports.** Each song gets a folder with `report.md` and `report.json` (the settings, each phase with its gates,
tries, tokens and time, the final check lines), its OGG (fetched from FamiTracker's `/files/` route), and
`events.jsonl`, the live log the page shows.

**Robust.** Every model call and tool call has a time limit; a server that's down is waited for, with growing
pauses, up to a limit (2 hours by default); FamiTracker's "busy" (503) is waited out. A tool call that timed out is
never sent twice; the model is told to look first. If FamiTracker, the model or the wiki can't be had at all, the
song goes back to waiting and the queue pauses (the page says why), so a night of songs doesn't all fail. Stopping
the service, a restart or a pause leaves the song saved; it resumes at the phase it was in, from that phase's backup.

## Install on the EVO X3 (Bazzite)

Python 3 is already there; Songsmith needs nothing else (standard library only).

1. Copy the folder `tools/songsmith` to `~/songsmith` (from the repository, or the zip of the branch).
2. Start it once to write the settings, then stop it with Ctrl+C:

   ```
   python3 ~/songsmith/songsmith.py serve
   ```

   The settings are in `~/.local/share/songsmith/config.json`; the page's Settings change them too.
3. Run it as a service of your user, and keep it running when you're logged out:

   ```
   mkdir -p ~/.config/systemd/user
   cp ~/songsmith/songsmith.service ~/.config/systemd/user/
   systemctl --user daemon-reload
   systemctl --user enable --now songsmith
   loginctl enable-linger $USER
   ```

   `journalctl --user -u songsmith -f` shows what it says. The page is at `http://<the EVO X3>:8765/` from any
   computer or phone on the home network or Tailscale (other addresses are refused). On Bazzite the firewall may
   need the port: `sudo firewall-cmd --permanent --add-port=8765/tcp && sudo firewall-cmd --reload`.
4. Open the page, go to **Settings**, fill in the model, FamiTracker and the wiki (below), **Save**, then
   **Test connections**: each line should be green, and the wiki line lists the pages it found (it warns when
   `AGENTS.md` or a kind page is missing).

### The model

Any OpenAI-compatible server with tool calls: llama.cpp's `llama-server` started with `--jinja`, or llama-swap in
front of it. Settings: the address with `/v1` (`http://127.0.0.1:8080/v1`) and the model's name as the server knows
it (`qwen3.8-flash-next`). An API key only if your server asks for one. Give the model a context of 32k tokens or
more: the plan phase carries the kind page, an example, Craft moves and the verdicts (about 10k tokens).

### FamiTracker on the Windows PC

1. In FamiTracker: **File > Connect Claude Code...**, tick **Let Claude Code connect**.
2. Under **Other computers on your network**, tick **Allow connections from other computers on my network**. The
   column shows this PC's addresses and the **access code**, each with a Copy button. Windows asks once whether
   FamiTracker may use the network: allow **Private networks**.
3. Leave **Let Claude change the song directly** on (Songsmith writes into the song).
4. In Songsmith's Settings: FamiTracker's address without `/mcp` (`http://192.168.1.10:46600`, or the Tailscale
   address `http://100.x.y.z:46600`), and the access code. Songsmith picks `/mcp/core` (the small set for local
   models) or `/mcp` (every tool, a song's "full tool set" box) itself.

FamiTracker must be running with the song window free. If another song is open with changes not saved, Songsmith
saves it before it starts its own (or waits, if you set that). Leave FamiTracker alone while songs are made: it
works on whatever song is open.

### The wiki

The folder of the FamiTracker wiki in the vault, `/home/blaxun/Documents/Hermes-Memory/FamiTracker Wiki` by
default. Songsmith reads it for every song, so your changes count from the next song on.

## Using the page

- **Add a song**: a name, the map in a few sentences (what the place is, what happens, how it feels), the kind and
  a flavour, the chip, the length, a tempo hint if you like, and the options (an intro, drums, ambience, swing, the
  full tool set). **Versions** makes several songs from one entry (each with another example and form);
  **priority** high puts it before the others. The buttons above the form fill it from a template; **Save as
  template** keeps your own.
- **The queue**: each song's state (waiting, phase N of 8, done, failed); arrows move a waiting song, **Skip** sets
  it aside, **Stop** ends a running one after its current step, **Retry** goes on from the phase it stopped in,
  **Pause queue** stops the running song after its current step (saved; it goes on later from the start of that
  phase, from its backup) and starts nothing new.
- **A song's page**: the phases, the live log (the model's words, every tool call, the gates; tick "tool replies"
  for FamiTracker's answers), the gates, the OGG to listen to, the report, **Add another like this** (the form
  filled from it) and **Make it again from the start**. Write what you hear in the verdict box and **File
  verdict**: your words go into the wiki word for word.
- The page works on a phone and follows the system's light or dark mode (the ◐ button switches).

## Command line

```
python3 songsmith.py serve [--data DIR] [--host H] [--port N]   # the page and the worker
python3 songsmith.py test  [--data DIR]                          # test the connections and the wiki
python3 songsmith.py run   [--data DIR]                          # work the queue once, without the page
```

## Tests

```
cd tools/songsmith/tests && python3 -m unittest discover -s .
```

They run every phase with a scripted model (`fakes.py`'s `ScriptedModel`, an OpenAI-compatible server) against a
fake FamiTracker (`FakeFamiTracker`, an MCP server keeping a small song), on a small wiki of their own
(`tests/wiki`): gates and retries, the harness's own fixes, pauses, restarts and resuming from a backup, servers
down or busy, time limits, the wiki tools and filing, and the web API. `smoke_real.py` runs one short song through a
real FamiTracker with the scripted model (a copy of the wiki gets the song log).

"""Fakes for the tests: an OpenAI-compatible model server that plays a script of tool calls, and an MCP server that
keeps a small song in memory the way FamiTracker's tools answer."""

import copy
import json
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

VRC6 = ["2A03 Pulse 1", "2A03 Pulse 2", "2A03 Triangle", "2A03 Noise", "2A03 DPCM", "VRC6 Pulse 1", "VRC6 Pulse 2",
        "VRC6 Sawtooth"]
TOOLS = ["get_song", "what_next", "read_notes", "write_notes", "add_instrument", "design_instrument", "set_instrument",
         "set_tempo", "mix", "set_swing", "add_chords_and_bass", "add_drums", "add_ambience", "add_harmony",
         "grow_melody", "polish", "add_echo", "check_loop", "fix_loop", "set_loop", "insert_bars", "name_section",
         "song_shape", "set_key", "export_for_game", "finish_song", "listen", "set_expansion", "copy_bars",
         "vary_part", "reference_sound", "find_sound", "review_arrangement", "check_song", "play", "stop",
         "save_song", "new_song", "open_song", "create_backup", "list_backups", "restore_backup", "get_recipe",
         "explain", "write_log", "read_user_notes", "measure_mix", "add_counter_melody", "transpose", "undo"]
READ_ONLY = {"get_song", "what_next", "read_notes", "check_song", "review_arrangement", "measure_mix", "listen",
             "check_loop", "get_recipe", "explain", "find_sound", "list_backups", "read_user_notes", "save_song",
             "create_backup", "restore_backup", "open_song", "new_song", "write_log", "play", "stop",
             "export_for_game", "finish_song"}


def serve(handler_class):
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler_class)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


class Song:
    def __init__(self, name="Untitled", expansion=("VRC6",)):
        self.name = name
        self.file = name + ".ftm"
        self.channels = {c: {} for c in (VRC6 if "VRC6" in expansion else VRC6[:5])}
        self.instruments = []
        self.sections = []
        self.bars = 8
        self.key = "C major"
        self.key_chosen = False
        self.tempo = "120 BPM (speed 6, tempo 150, NTSC 60 Hz); 4 rows a beat, 4 beats a bar (16 rows)"

    def text(self):
        lines = ['"%s", file %s' % (self.name, self.file), "Tempo: " + self.tempo,
                 "Length: %d bars (2 frames of 64 rows)" % self.bars]
        if self.sections:
            lines.append("Sections: " + ", ".join('"%s" bars %d-%d' % s for s in self.sections))
        lines.append("Channels (expansion: VRC6):")
        for i, (name, bars) in enumerate(self.channels.items()):
            n = sum(1 for toks in bars.values() for t in toks if t not in ("-", "."))
            lines.append("  %d %s: %d notes in bars 1-%d, in the middle" % (i + 1, name, n, self.bars) if n else
                         "  %d %s: no notes" % (i + 1, name))
        lines.append("Key: %s%s" % (self.key, " (chosen for this track: the tools use it)" if self.key_chosen else ""))
        lines.append("Instruments: " + " ".join('%02X "%s" (VRC6)' % (i, n) for i, n in enumerate(self.instruments)))
        return "\n".join(lines)


class FakeFamiTracker:
    """the state and the tools; the HTTP side calls answer()"""

    def __init__(self, token="secret"):
        self.token = token
        self.song = Song()
        self.dirty = False
        self.disk = {}           # name -> saved Song
        self.backups = []        # (label, Song)
        self.files = {}          # OGG name -> bytes
        self.calls = []          # (tool, arguments)
        self.findings = []       # check_song's findings worth fixing; polish clears them
        self.fix_clears = True
        self.score = 8.5
        self.busy = 0            # answer 503 this many times
        self.lock = threading.Lock()
        self.server = None

    @property
    def url(self):
        return "http://127.0.0.1:%d" % self.server.server_address[1]

    def start(self):
        fake = self

        class H(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_POST(self):
                if self.headers.get("Authorization") != "Bearer " + fake.token:
                    return self.reply(401, {"error": "bad code"})
                if fake.busy > 0:
                    fake.busy -= 1
                    return self.reply(503, {"error": "busy for 3 s"})
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                result = fake.rpc(body, self.path)
                return self.reply(200, {"jsonrpc": "2.0", "id": body.get("id"), "result": result})

            def do_GET(self):
                m = re.match(r"^/files/(.+)$", self.path)
                if not m or self.headers.get("Authorization") != "Bearer " + fake.token:
                    return self.reply(404, {})
                import urllib.parse
                data = fake.files.get(urllib.parse.unquote(m.group(1)))
                if data is None:
                    return self.reply(404, {})
                self.send_response(200)
                self.send_header("Content-Type", "audio/ogg")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def reply(self, status, body):
                data = json.dumps(body).encode()
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
        self.server = serve(H)
        return self

    def stop(self):
        if self.server:
            self.server.shutdown()
            self.server.server_close()

    def rpc(self, body, path):
        method = body.get("method")
        if method == "initialize":
            return {"protocolVersion": "2025-06-18", "capabilities": {"tools": {}}, "instructions": "fake"}
        if method == "tools/list":
            names = TOOLS if path == "/mcp" else [t for t in TOOLS if t not in ("add_counter_melody", "transpose",
                                                                                 "measure_mix", "undo")]
            return {"tools": [{"name": n, "description": "The %s tool." % n,
                               "inputSchema": {"type": "object", "properties": {}}} for n in names]}
        if method == "resources/list":
            return {"resources": [{"uri": "famitracker://files/" + n} for n in self.files]}
        if method == "tools/call":
            p = body.get("params", {})
            with self.lock:
                self.calls.append((p.get("name"), p.get("arguments", {})))
                text, error, content = self.tool(p.get("name"), p.get("arguments") or {})
            return {"content": [{"type": "text", "text": text}] + content, "isError": error}
        return {}

    def names(self):
        return [c[0] for c in self.calls]

    # ---- the tools
    def tool(self, name, a):
        s = self.song
        if name not in READ_ONLY:
            self.dirty = True
        if name == "new_song":
            if self.dirty:
                return "Refused: the open song has changes that aren't saved. save_song first.", True, []
            base = a.get("name") or "Untitled"
            n, name2 = 2, base
            while name2 in self.disk:
                name2 = "%s %d" % (base, n)
                n += 1
            self.song = Song(name2, a.get("expansion", ["VRC6"]))
            self.disk[name2] = copy.deepcopy(self.song)
            return "A new song \"%s\", saved in the Songs folder." % name2, False, []
        if name == "open_song":
            if self.dirty:
                return "Refused: the open song has changes that aren't saved.", True, []
            if a.get("name") not in self.disk:
                return "No song \"%s\"." % a.get("name"), True, []
            self.song = copy.deepcopy(self.disk[a["name"]])
            return "Opened \"%s\"." % a["name"], False, []
        if name == "save_song":
            self.disk[s.name] = copy.deepcopy(s)
            self.dirty = False
            return "Saved %s." % s.file, False, []
        if name == "create_backup":
            self.backups.append((a.get("label", ""), copy.deepcopy(s), self.dirty))
            return "Backup %d: %s" % (len(self.backups), a.get("label")), False, []
        if name == "restore_backup":
            for label, song, _ in reversed(self.backups):
                if label == a.get("backup"):
                    self.song = copy.deepcopy(song)
                    self.dirty = True
                    return "Restored \"%s\"." % label, False, []
            return "No backup %s." % a.get("backup"), True, []
        if name == "get_song":
            return s.text(), False, []
        if name == "set_key":
            s.key, s.key_chosen = a.get("key", "C major"), True
            return "The key is %s." % s.key, False, []
        if name == "song_shape":
            s.sections = [("Intro", 1, 4), ("A", 5, 12), ("A'", 13, 20), ("B", 21, 28)]
            s.bars = 28
            return "Laid out: Intro 4, A 8, A' 8, B 8.", False, []
        if name == "design_instrument":
            s.instruments.append(a.get("name") or "Sound %d" % len(s.instruments))
            return "Designed \"%s\". Nothing vanishes." % s.instruments[-1], False, []
        if name in ("grow_melody", "write_notes"):
            ch = a.get("channel", "VRC6 Pulse 1")
            figures = ["C4 - E4 G4 - . A4 -", "G4 - - E4 D4 - . .", "E4 F4 G4 - A4 - G4 -", "D4 - - - C4 - . ."]
            for b in range(5, s.bars + 1):
                s.channels.setdefault(ch, {})[b] = figures[b % 4].split()
            return "Wrote the melody on %s." % ch, False, []
        if name == "add_chords_and_bass":
            for b in range(1, s.bars + 1):
                s.channels["2A03 Pulse 1"][b] = "C3 - - - G3 - - -".split() if b % 2 else "A2 - E3 - - - . .".split()
                s.channels["2A03 Triangle"][b] = "C2 - . C2 G1 - . ." .split() if b % 3 else "F2 - - - F2 . C2 -".split()
                s.channels.get("VRC6 Sawtooth", {})[b] = "E3 - - - - - . .".split()
            return "Chords and a bass in bars 1-%d." % s.bars, False, []
        if name == "add_drums":
            for b in range(1, s.bars + 1):
                s.channels["2A03 Noise"][b] = "C-# . C-# . C-# . C-# ." .split()
                s.channels["2A03 DPCM"][b] = "C3 . . . D3 . . ." .split() if b % 2 else "C3 . C3 . D3 . . D3".split()
            return "Drums in bars 1-%d." % s.bars, False, []
        if name in ("add_echo", "add_counter_melody", "add_harmony"):
            for b in range(5, s.bars + 1):
                s.channels["VRC6 Pulse 2"][b] = ". . . . . . G4 E4".split() if b % 2 else ". . . . C5 - . .".split()
                s.channels["2A03 Pulse 2"][b] = ". . E4 - . . . .".split()
            return "A second line.", False, []
        if name == "read_notes":
            ch = s.channels.get(a.get("channel"), {})
            lo, hi = a.get("from_bar", 1), a.get("to_bar", s.bars)
            lines = ["%s, bars %d-%d in eighths:" % (a.get("channel"), lo, hi), 'Instrument "Lead" (00).']
            for b in range(lo, hi + 1):
                lines.append("%d: %s" % (b, " ".join(ch.get(b, ["."] * 8))))
            return "\n".join(lines), False, []
        if name == "check_song":
            if a.get("format") == "json":
                return json.dumps({"counts": {"clash": len(self.findings)}, "findings": self.findings}), False, []
            return "check_song: %d findings." % len(self.findings), False, []
        if name == "polish":
            if self.fix_clears:
                self.findings = []
            return "Polished.", False, []
        if name == "review_arrangement":
            return ("Review (lively), bars 1-%d in C major: %s/10.\nParts: melody on VRC6 Pulse 1; chords on 2A03 "
                    "Pulse 1; bass on 2A03 Triangle; drums on 2A03 Noise.\nWhat would help: nothing." % (
                        s.bars, self.score)), False, []
        if name == "measure_mix":
            return ("Measured bars 1-%d: the loop (bars 5-%d) -20.1 LUFS, peak -3.0 dBFS.\nAgainst the melody "
                    "(VRC6 Pulse 1), where both play: Triangle (bass) 1.2 dB over it, 2A03 Pulse 1 5.0 dB under."
                    % (s.bars, s.bars)), False, []
        if name in ("finish_song", "export_for_game"):
            ogg = s.name + ".ogg"
            self.files[ogg] = b"OggS-fake-" + s.name.encode()
            self.disk[s.name] = copy.deepcopy(s)
            self.dirty = False
            return ("Polished, mixed. Listen: nothing sounds wrong. Saved %s next to the song." % ogg, False,
                    [{"type": "resource_link", "uri": "famitracker://files/" + ogg, "name": ogg}])
        return "%s done." % name, False, []


# ---- the model
DEFAULT_CALLS = {
    "plan": [],
    "start": [("set_tempo", {"bpm": 120}), ("set_key", {"key": "C major"}), ("song_shape", {"kind": "lively"})],
    "sounds": [("design_instrument", {"name": "Lead"}), ("design_instrument", {"name": "Bell"}),
               ("design_instrument", {"name": "Bass"}), ("design_instrument", {"name": "Pad"})],
    "hook": [("grow_melody", {"motif": "C4 - E4 G4 - . A4 -", "channel": "VRC6 Pulse 1"})],
    "parts": [("add_chords_and_bass", {}), ("add_drums", {}), ("add_echo", {})],
    "development": [("vary_part", {"variation": "restrike"})],
    "checks": [("check_song", {}), ("review_arrangement", {})],
    "finish": [("finish_song", {"kind": "lively"})],
}

PLAN = """Key C major, 120 BPM, Intro 4, A 8, A' 8, B 8.
| Channel | Intro | A | A' | B |
|---|---|---|---|---|
| VRC6 Pulse 1 | pickup | hook | hook | own tune |
| 2A03 Pulse 1 | chords | chords | chords struck | broken |
| Triangle | bass | bass | walking | half |
Hook idea: a leap up and falling steps."""

PHASE_ID = {"Plan": "plan", "Start": "start", "Sounds": "sounds", "Hook and melody": "hook", "Parts": "parts",
            "Development": "development", "Checks": "checks", "Finish": "finish"}


class ScriptedModel:
    """answers chat requests: each phase's calls in turn, then phase_done; a fix request gets fix_calls"""

    def __init__(self, calls=None, fix_calls=None, delay=0.0):
        self.calls = calls or DEFAULT_CALLS
        self.fix_calls = fix_calls if fix_calls is not None else [("polish", {"touches": ["clashes"]})]
        self.requests = []
        self.delay = delay
        self.extra = None        # a function (phase, messages) -> message, to take over
        self.server = None
        self.fail = 0            # answer 503 this many times
        self.lock = threading.Lock()

    @property
    def url(self):
        return "http://127.0.0.1:%d/v1" % self.server.server_address[1]

    def start(self):
        model = self

        class H(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                if model.fail > 0:
                    model.fail -= 1
                    return self.reply(503, {"error": "loading model"})
                if model.delay:
                    time.sleep(model.delay)
                with model.lock:
                    model.requests.append(body)
                    message = model.respond(body["messages"], body.get("tools") or [])
                self.reply(200, {"choices": [{"message": message}],
                                 "usage": {"prompt_tokens": 1000, "completion_tokens": 50}})

            def do_GET(self):
                self.reply(200, {"data": [{"id": "fake-model"}]})

            def reply(self, status, body):
                data = json.dumps(body).encode()
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                try:
                    self.wfile.write(data)
                except OSError:
                    pass
        self.server = serve(H)
        return self

    def stop(self):
        if self.server:
            self.server.shutdown()
            self.server.server_close()

    def respond(self, messages, tools):
        system = messages[0]["content"]
        m = re.search(r"This is phase \d+ of \d+: ([^.]+)\.", system)
        phase = PHASE_ID.get(m.group(1), "plan") if m else "plan"
        if self.extra:
            out = self.extra(phase, messages)
            if out is not None:
                return out
        users = [x for x in messages if x["role"] == "user"]
        fixing = "found what to fix" in users[0]["content"]
        plan = phase == "plan" and not fixing
        script = self.fix_calls if fixing else self.calls.get(phase, [])
        done = sum(1 for x in messages if x["role"] == "assistant")
        if plan:
            return {"role": "assistant", "content": PLAN}
        if done < len(script):
            name, args = script[done]
            return {"role": "assistant", "content": "", "tool_calls": [{
                "id": "call_%d" % done, "type": "function",
                "function": {"name": name, "arguments": json.dumps(args)}}]}
        return {"role": "assistant", "content": "", "tool_calls": [{
            "id": "done", "type": "function",
            "function": {"name": "phase_done", "arguments": json.dumps({"summary": "%s done." % phase})}}]}


def finding(kind="clash", severity="high"):
    return {"kind": kind, "severity": severity, "message": "Clash at bar 6.", "summary": "a clash",
            "fix": {"tool": "polish", "arguments": {"touches": ["clashes"], "from_bar": 6, "to_bar": 6},
                    "does": "moves the note"}}

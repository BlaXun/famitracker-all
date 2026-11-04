"""The web page and its JSON API (standard library http.server). For the home network and Tailscale only:
requests from other addresses are refused."""

import ipaddress
import json
import mimetypes
import os
import re
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from .config import CHIPS, FLAVOURS, KINDS, LENGTHS, load_json, save_json
from .llm import ModelClient
from .mcp import McpClient
from .net import Patience
from .phases import PHASES
from .wiki import Wiki, today

STATIC = os.path.join(os.path.dirname(__file__), "static")

TEMPLATES = [
    {"label": "Cozy shop", "name": "Corner Shop", "kind": "cozy", "flavour": "", "chip": "VRC6",
     "about": "A small shop in the village where a friendly keeper sells odds and ends. Warm, by day, a bell on the door."},
    {"label": "Night village", "name": "Village at Night", "kind": "calm", "flavour": "", "chip": "VRC6",
     "about": "The village after dark: lanterns, crickets, everyone asleep. Calm and safe.", "options": {"ambience": True}},
    {"label": "Frozen cave", "name": "Crystal Cave", "kind": "mysterious", "flavour": "cold", "chip": "VRC6",
     "about": "An ice cave full of glittering crystals and dripping water. Curious, not scary."},
    {"label": "Field stage", "name": "Meadow Run", "kind": "lively", "flavour": "nature", "chip": "VRC6",
     "about": "A bright stage across meadows and hills, jumping over streams. Cheerful and quick."},
    {"label": "Airship", "name": "Sky Voyage", "kind": "adventure", "flavour": "airy", "chip": "VRC6",
     "about": "Flying the airship above the clouds to a new island. Wide, hopeful, on the move."},
    {"label": "Boss fight", "name": "Forge Guardian", "kind": "action", "flavour": "hot", "chip": "VRC6",
     "about": "A fight with a golem in the lava forge. Driving, heavy, exciting.", "length": "short"},
    {"label": "Festival", "name": "Harvest Fair", "kind": "festive", "flavour": "", "chip": "VRC6",
     "about": "The yearly market festival: stalls, dancing, lanterns strung over the square."},
    {"label": "Haunted library", "name": "Dusty Archive", "kind": "tense", "flavour": "spooky", "chip": "2A03 only",
     "about": "Sneaking through a haunted library where books float. Careful steps.", "options": {"drums": False}},
]


def private(address):
    """home networks, Tailscale and this computer (as FamiTracker's own connection allows)"""
    try:
        ip = ipaddress.ip_address(address.split("%")[0])
    except ValueError:
        return False
    if ip.version == 6 and ip.ipv4_mapped:
        ip = ip.ipv4_mapped
    if ip.is_loopback or ip.is_private or ip.is_link_local:
        return True
    return ip.version == 4 and ip in ipaddress.ip_network("100.64.0.0/10")


class App:
    """the web side: the store, the settings and the runner, as the handler reaches them"""

    def __init__(self, store, config, runner, data_dir):
        self.store = store
        self.config = config
        self.runner = runner
        self.data_dir = data_dir
        self.templates_path = os.path.join(data_dir, "templates.json")

    def templates(self):
        return TEMPLATES + (load_json(self.templates_path, []) or [])

    def state(self):
        current = self.runner.current if self.runner else None
        return {"paused": self.store.paused(), "message": self.store.message(), "jobs": self.store.listing(),
                "current": current["id"] if current else None, "kinds": KINDS, "flavours": FLAVOURS,
                "chips": list(CHIPS), "lengths": list(LENGTHS), "phases": [p["title"] for p in PHASES],
                "templates": self.templates(), "time": time.time()}

    def test(self, changes=None):
        """test the connections with the settings (and changes not saved yet)"""
        from .config import merged
        s = merged(self.config.get(), changes or {})
        out = {}
        patience = Patience(wait_minutes=0.2)
        try:
            model = ModelClient(s["model"]["base_url"], s["model"]["name"], s["model"].get("api_key", ""), 60,
                                patience=patience)
            names = model.models()
            ok = s["model"]["name"] in names if names else True
            out["model"] = {"ok": ok, "text": ("Found the model \"%s\"." % s["model"]["name"]) if ok and names else
                            ("The server answers; it lists: %s" % ", ".join(names[:12]) if names else
                             "The server answers.")}
        except Exception as error:
            out["model"] = {"ok": False, "text": "Can't reach the model at %s: %s" % (s["model"]["base_url"], error)}
        try:
            mcp = McpClient(s["famitracker"]["url"], s["famitracker"]["token"], timeout=30, patience=patience)
            mcp.connect()
            full = len(mcp.list_tools())
            core = McpClient(s["famitracker"]["url"], s["famitracker"]["token"], core=True, timeout=30,
                             patience=patience)
            small = len(core.list_tools())
            song = mcp.call("get_song", {})
            first = song.text.split("\n")[0] if not song.is_error else "no song open"
            out["famitracker"] = {"ok": True, "text": "Connected: %d tools (the small set %d). Open now: %s"
                                  % (full, small, first[:120])}
        except Exception as error:
            out["famitracker"] = {"ok": False, "text": "Can't reach FamiTracker at %s: %s" % (
                s["famitracker"]["url"], error)}
        wiki = Wiki(s["wiki_path"])
        check = wiki.check(KINDS)
        out["wiki"] = {"ok": wiki.exists() and not check["warnings"], "text": (
            "%d pages; kinds found: %d of %d; examples: %d." % (check["pages"], len(check["kinds"]), len(KINDS),
                                                              len(check["examples"]))),
            "warnings": check["warnings"], "missing": check["missing"], "missing_kinds": check["missing_kinds"],
            "examples": check["examples"]}
        return out

    def verdict(self, job_id, words):
        job = self.store.get(job_id)
        if not job:
            raise KeyError(job_id)
        words = (words or "").strip()
        if not words:
            raise ValueError("Write the verdict first.")
        s = self.config.get()
        wiki = Wiki(s["wiki_path"])
        if not wiki.exists():
            raise ValueError("The wiki isn't at %s." % s["wiki_path"])
        song = (job.get("song") or {}).get("name") or job["title"]
        changed = wiki.file_verdict(song, words, job.get("song_log"))
        job.setdefault("verdicts", []).append({"date": today(), "words": words, "filed": changed})
        self.store.save(job)
        self.store.event(job, "verdict", "Filed the verdict (%s): \"%s\"" % (", ".join(changed), words))
        return changed


class Handler(BaseHTTPRequestHandler):
    server_version = "Songsmith/1"
    app = None

    def log_message(self, fmt, *args):
        pass

    def send(self, status, body, kind="application/json", extra=None):
        data = body if isinstance(body, bytes) else (
            json.dumps(body, ensure_ascii=False).encode("utf-8") if kind == "application/json" else body.encode("utf-8"))
        self.send_response(status)
        self.send_header("Content-Type", kind + ("; charset=utf-8" if kind.startswith(("text", "application/j"))
                                                 else ""))
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def body(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length > 1_000_000:
            raise ValueError("Too much data.")
        raw = self.rfile.read(length) if length else b"{}"
        return json.loads(raw.decode("utf-8") or "{}")

    def allowed(self):
        if not private(self.client_address[0]):
            self.send(403, {"error": "Only the home network and Tailscale may use Songsmith."})
            return False
        return True

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        if not self.allowed():
            return
        app = self.app
        url = urllib.parse.urlparse(self.path)
        path = url.path
        query = urllib.parse.parse_qs(url.query)
        try:
            if path in ("/", "/index.html"):
                with open(os.path.join(STATIC, "index.html"), "rb") as f:
                    return self.send(200, f.read(), "text/html")
            if path == "/api/state":
                return self.send(200, app.state())
            if path == "/api/settings":
                return self.send(200, app.config.public())
            m = re.match(r"^/api/jobs/([\w-]+)$", path)
            if m:
                job = app.store.get(m.group(1))
                return self.send(200, job) if job else self.send(404, {"error": "No such job."})
            m = re.match(r"^/api/jobs/([\w-]+)/events$", path)
            if m:
                after = int((query.get("after") or ["0"])[0])
                return self.send(200, {"events": app.store.events(m.group(1), after)})
            m = re.match(r"^/api/jobs/([\w-]+)/files/(.+)$", path)
            if m:
                return self.file(m.group(1), urllib.parse.unquote(m.group(2)))
            return self.send(404, {"error": "Not found."})
        except Exception as error:
            return self.send(500, {"error": str(error)})

    def file(self, job_id, name):
        if "/" in name or "\\" in name or name.startswith(".") or not re.match(r"^[^<>:\"|?*]+\.(ogg|md|json)$", name):
            return self.send(400, {"error": "Not a file of the job."})
        path = os.path.join(self.app.store.job_dir(job_id), name)
        if not os.path.isfile(path):
            return self.send(404, {"error": "No such file."})
        with open(path, "rb") as f:
            data = f.read()
        kind = mimetypes.guess_type(name)[0] or ("audio/ogg" if name.endswith(".ogg") else "text/plain")
        if name.endswith(".md"):
            kind = "text/plain"
        extra = {"Accept-Ranges": "bytes"}
        m = re.match(r"^bytes=(\d*)-(\d*)$", self.headers.get("Range") or "")
        if m and data:
            start = int(m.group(1)) if m.group(1) else max(0, len(data) - int(m.group(2) or 0))
            end = int(m.group(2)) if m.group(1) and m.group(2) else len(data) - 1
            end = min(end, len(data) - 1)
            if start > end:
                return self.send(416, b"", kind, {"Content-Range": "bytes */%d" % len(data)})
            extra["Content-Range"] = "bytes %d-%d/%d" % (start, end, len(data))
            return self.send(206, data[start:end + 1], kind, extra)
        return self.send(200, data, kind, extra)

    def do_POST(self):
        if not self.allowed():
            return
        app = self.app
        path = urllib.parse.urlparse(self.path).path
        if self.headers.get("Origin") and urllib.parse.urlparse(self.headers["Origin"]).netloc != self.headers.get("Host"):
            return self.send(403, {"error": "Requests from other pages are refused."})
        try:
            data = self.body()
            if path == "/api/jobs":
                jobs = app.store.add(data)
                return self.send(200, {"jobs": [j["id"] for j in jobs]})
            if path in ("/api/queue/pause", "/api/queue/resume"):
                app.store.set_paused(path.endswith("pause"))
                return self.send(200, {"paused": app.store.paused()})
            if path == "/api/settings":
                allowed = {k: v for k, v in data.items() if k in ("model", "famitracker", "wiki_path", "limits",
                                                                  "gates", "file_song_logs", "unsaved_song")}
                for section, secret in (("famitracker", "token"), ("model", "api_key")):
                    if isinstance(allowed.get(section), dict) and not allowed[section].get(secret):
                        allowed[section].pop(secret, None)     # empty: keep the one saved
                    if isinstance(allowed.get(section), dict):
                        allowed[section].pop(secret + "_set", None)
                app.config.update(allowed)
                return self.send(200, app.config.public())
            if path == "/api/test":
                changes = {k: v for k, v in data.items() if k in ("model", "famitracker", "wiki_path")}
                for section, secret in (("famitracker", "token"), ("model", "api_key")):
                    if isinstance(changes.get(section), dict) and not changes[section].get(secret):
                        changes[section].pop(secret, None)
                return self.send(200, app.test(changes))
            if path == "/api/templates":
                own = load_json(app.templates_path, []) or []
                entry = {k: data.get(k) for k in ("label", "name", "about", "kind", "flavour", "chip", "length",
                                                  "tempo", "options") if data.get(k) is not None}
                entry["label"] = (entry.get("label") or entry.get("name") or "My template")[:40]
                own = [t for t in own if t.get("label") != entry["label"]] + [entry]
                save_json(app.templates_path, own)
                return self.send(200, {"templates": app.templates()})
            m = re.match(r"^/api/jobs/([\w-]+)/(cancel|skip|retry|restart|remove|up|down|top|verdict)$", path)
            if m:
                job_id, action = m.groups()
                if action in ("up", "down", "top"):
                    step = {"up": -1, "down": 1, "top": -10_000}[action]
                    return self.send(200, {"ok": app.store.move(job_id, step)})
                if action == "verdict":
                    return self.send(200, {"filed": app.verdict(job_id, data.get("words"))})
                result = app.store.control(job_id, action)
                return self.send(200 if result else 404, result or {"error": "No such job."})
            return self.send(404, {"error": "Not found."})
        except (ValueError, KeyError) as error:
            return self.send(400, {"error": str(error)})
        except Exception as error:
            return self.send(500, {"error": "%s: %s" % (type(error).__name__, error)})


def serve(app, host, port):
    handler = type("SongsmithHandler", (Handler,), {"app": app})
    server = ThreadingHTTPServer((host, port), handler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, name="songsmith-web", daemon=True)
    thread.start()
    return server

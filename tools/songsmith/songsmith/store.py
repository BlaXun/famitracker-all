"""The job queue on disk: data/queue.json (the order, paused or not) and a folder per job (job.json, events.jsonl,
the report and the OGG). Every change is written at once, so a restart finds the queue as it was."""

import json
import os
import threading
import time

from .config import CHIPS, FLAVOURS, KINDS, LENGTHS, load_json, save_json
from .phases import PHASES

OPTIONS = {"ambience": False, "drums": True, "swing": False, "intro": True, "full_tools": False}
EVENT_CHARS = 4000


class Store:
    def __init__(self, folder):
        self.folder = folder
        self.lock = threading.RLock()
        self.serial = 0
        os.makedirs(os.path.join(folder, "jobs"), exist_ok=True)
        self.queue_path = os.path.join(folder, "queue.json")
        self.queue = load_json(self.queue_path, {"paused": False, "order": []})
        self.jobs = {}
        for job_id in list(self.queue["order"]):
            job = load_json(self.job_path(job_id))
            if job:
                self.jobs[job_id] = job
            else:
                self.queue["order"].remove(job_id)
        for job in self.jobs.values():     # a job that ran when the harness stopped picks up at its phase
            if job["status"] == "running":
                job["status"] = "waiting"
                job["resume"] = True
                self.note(job, "The harness stopped during phase %d; it resumes there." % (job["phase"] + 1))
                self.save(job)

    # ---- paths
    def job_dir(self, job_id):
        return os.path.join(self.folder, "jobs", job_id)

    def job_path(self, job_id):
        return os.path.join(self.job_dir(job_id), "job.json")

    def save(self, job):
        with self.lock:
            job["updated"] = time.time()
            save_json(self.job_path(job["id"]), job)

    def save_queue(self):
        with self.lock:
            save_json(self.queue_path, self.queue)

    # ---- jobs
    def new_id(self):
        with self.lock:
            stamp = time.strftime("%Y%m%d-%H%M%S")
            n = 1
            while "%s-%d" % (stamp, n) in self.jobs:
                n += 1
            return "%s-%d" % (stamp, n)

    def add(self, form):
        """jobs from the page's form (one per version); returns them"""
        title = (form.get("name") or "").strip()
        if not title:
            raise ValueError("A song needs a name.")
        kind = form.get("kind", "lively")
        if kind not in KINDS:
            raise ValueError("Unknown kind: %s" % kind)
        flavour = (form.get("flavour") or "").strip()
        if flavour and flavour not in FLAVOURS:
            raise ValueError("Unknown flavour: %s" % flavour)
        chip = form.get("chip", "VRC6")
        if chip not in CHIPS:
            raise ValueError("Unknown chip: %s" % chip)
        length = form.get("length", "normal")
        if length not in LENGTHS:
            raise ValueError("Unknown length: %s" % length)
        versions = max(1, min(8, int(form.get("versions") or 1)))
        options = dict(OPTIONS)
        options.update({k: bool(v) for k, v in (form.get("options") or {}).items() if k in OPTIONS})
        priority = max(-1, min(1, int(form.get("priority") or 0)))
        out = []
        with self.lock:
            for v in range(1, versions + 1):
                job_id = self.new_id()
                job = {
                    "id": job_id, "created": time.time(), "updated": time.time(),
                    "title": title if versions == 1 else "%s (v%d)" % (title, v),
                    "base_title": title, "about": (form.get("about") or "").strip()[:2000], "kind": kind,
                    "flavour": flavour, "chip": chip, "length": length, "tempo": (form.get("tempo") or "").strip()[:80],
                    "options": options, "version": v, "versions": versions, "priority": priority,
                    "status": "waiting", "phase": 0, "control": None, "resume": False,
                    "song": None, "plan": "", "phases": [], "notes": [], "usage": {"prompt": 0, "completion": 0,
                                                                                    "calls": 0, "tool_calls": 0},
                    "started": None, "ended": None, "seconds": 0, "error": "", "ogg": [], "final": {},
                    "verdicts": [], "song_log": None,
                }
                self.jobs[job_id] = job
                self.place(job_id, priority)
                self.save(job)
                self.event(job, "queued", "Added to the queue.")
                out.append(job)
            self.save_queue()
        return out

    def place(self, job_id, priority):
        """into the order: after the last job of its priority or higher"""
        order = self.queue["order"]
        at = len(order)
        for i, other in enumerate(order):
            o = self.jobs.get(other)
            if o and o["status"] == "waiting" and o.get("priority", 0) < priority:
                at = i
                break
        order.insert(at, job_id)

    def get(self, job_id):
        return self.jobs.get(job_id)

    def listing(self):
        with self.lock:
            return [summary(self.jobs[j]) for j in self.queue["order"] if j in self.jobs]

    def next_job(self):
        with self.lock:
            if self.queue["paused"]:
                return None
            for job_id in self.queue["order"]:
                job = self.jobs.get(job_id)
                if job and job["status"] == "waiting":
                    return job
            return None

    def move(self, job_id, step):
        with self.lock:
            order = self.queue["order"]
            if job_id not in order:
                return False
            i = order.index(job_id)
            j = max(0, min(len(order) - 1, i + step))
            order.insert(j, order.pop(i))
            self.save_queue()
            return True

    def set_paused(self, paused):
        with self.lock:
            self.queue["paused"] = bool(paused)
            self.save_queue()

    def paused(self):
        return self.queue["paused"]

    def control(self, job_id, action):
        """cancel, skip or retry a job"""
        with self.lock:
            job = self.jobs.get(job_id)
            if not job:
                return None
            if action in ("cancel", "skip"):
                if job["status"] == "running":
                    job["control"] = action
                    self.event(job, "note", "Asked to %s; it stops after the current step." % action)
                elif job["status"] == "waiting":
                    job["status"] = "cancelled" if action == "cancel" else "skipped"
                    self.event(job, "note", job["status"].capitalize() + ".")
            elif action == "retry":
                if job["status"] in ("failed", "cancelled", "skipped"):
                    job["status"] = "waiting"
                    job["error"] = ""
                    job["control"] = None
                    job["resume"] = job["song"] is not None
                    self.event(job, "note", "Back in the queue; it starts again at phase %d." % (job["phase"] + 1))
            elif action == "restart":
                if job["status"] != "running":
                    job.update({"status": "waiting", "phase": 0, "song": None, "phases": [], "plan": "",
                                "control": None, "resume": False, "error": "", "ogg": [], "final": {}})
                    self.event(job, "note", "Back in the queue from the start (a new song).")
            elif action == "remove":
                if job["status"] != "running":
                    self.queue["order"].remove(job_id)
                    del self.jobs[job_id]
                    self.save_queue()
                    return {"removed": job_id}
            self.save(job)
            return job

    # ---- the job's log, for the page
    def event(self, job, kind, text, data=None):
        with self.lock:
            self.serial += 1
            entry = {"t": time.time(), "kind": kind, "text": (text or "")[:EVENT_CHARS]}
            if data is not None:
                entry["data"] = data
            path = os.path.join(self.job_dir(job["id"]), "events.jsonl")
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "a", encoding="utf-8") as f:
                f.write(json.dumps(entry, ensure_ascii=False) + "\n")

    def note(self, job, text):
        job.setdefault("notes", []).append(text)
        self.event(job, "note", text)

    def events(self, job_id, after=0, limit=400):
        path = os.path.join(self.job_dir(job_id), "events.jsonl")
        out = []
        try:
            with open(path, encoding="utf-8") as f:
                for i, line in enumerate(f):
                    if i >= after:
                        try:
                            entry = json.loads(line)
                        except ValueError:
                            continue
                        entry["n"] = i
                        out.append(entry)
        except OSError:
            pass
        return out[-limit:]


def summary(job):
    """what the queue shows of a job"""
    phase = job["phase"]
    out = {k: job.get(k) for k in ("id", "title", "kind", "flavour", "chip", "status", "priority", "version",
                                   "versions", "created", "started", "ended", "seconds", "error", "ogg", "usage")}
    out["phase"] = phase
    out["phases"] = len(PHASES)
    out["phase_name"] = PHASES[phase]["title"] if phase < len(PHASES) else "Done"
    out["gates_failed"] = sum(1 for p in job.get("phases", []) for g in (p.get("gates") or [])[-1:]
                              for r in g if not r.get("ok"))
    out["song"] = (job.get("song") or {}).get("name")
    return out

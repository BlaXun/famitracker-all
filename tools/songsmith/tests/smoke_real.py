#!/usr/bin/env python3
"""End-to-end smoke run against a real FamiTracker (not part of the unit tests): one short song, the model's calls
scripted (no local model needed), every phase, gate, backup, save, the OGG download and the report.

  python3 smoke_real.py --url http://127.0.0.1:46520 --token CODE --wiki "/path/to/FamiTracker Wiki" [--keep DIR]

The wiki is copied first, so the song log it files lands in the copy. The song goes into FamiTracker's Songs folder
as "Songsmith test ..." (a new name each time)."""

import argparse
import json
import os
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from fakes import ScriptedModel                  # noqa: E402
from songsmith.config import Config              # noqa: E402
from songsmith.runner import Runner              # noqa: E402
from songsmith.store import Store                # noqa: E402

CALLS = {
    "start": [("set_tempo", {"bpm": 128.6}), ("set_key", {"key": "C major"}),
              ("song_shape", {"kind": "lively", "bars": 24, "variant": 1})],
    "sounds": [("design_instrument", {"name": "Meadow lead", "kind": "bloom lead", "channel": "VRC6 Pulse 1",
                                      "words": "a gentle vibrato"}),
               ("design_instrument", {"name": "Meadow bell", "kind": "bell", "channel": "2A03 Pulse 2"}),
               ("design_instrument", {"name": "Meadow bass", "kind": "bass", "channel": "2A03 Triangle"}),
               ("design_instrument", {"name": "Meadow chords", "kind": "pluck", "channel": "2A03 Pulse 1"})],
    "hook": [("wiki_read", {"page": "Craft moves", "heading": "1"}),
             ("grow_melody", {"motif": "C4 - E4 G4 - . A4 - | G4 - - - E4 - . .", "channel": "VRC6 Pulse 1",
                              "kind": "lively", "section": "A", "chords": "| C | Am | F | G | C | Am | Dm G | C |"}),
             ("grow_melody", {"motif": "C4 - E4 G4 - . A4 - | G4 - - - E4 - . .", "channel": "VRC6 Pulse 1",
                              "kind": "lively", "section": "A", "chords": "| C | Am | F | G | C | Am | Dm G | C |",
                              "pick": 1}),
             ("copy_bars", {"section": "A", "at_bar": 13, "channels": ["VRC6 Pulse 1"]})],
    "parts": [("add_chords_and_bass", {"chord_style": "broken", "bass_style": "root_fifth", "chords": "| C | Am | F | G | C | Am | Dm G | C |"}),
              ("add_drums", {"intensity": "light"}), ("add_echo", {"channel": "VRC6 Pulse 1", "to_channel": "VRC6 Pulse 2"})],
    "development": [("review_arrangement", {"kind": "lively", "format": "short"})],
    "checks": [("check_song", {"kind": "lively"})],
    "finish": [("finish_song", {"kind": "lively"})],
}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--url", default="http://127.0.0.1:46520")
    p.add_argument("--token", required=True)
    p.add_argument("--wiki", required=True)
    p.add_argument("--keep", help="a folder to keep the job's data in")
    p.add_argument("--name", default="Songsmith test Meadow")
    a = p.parse_args()
    tmp = tempfile.mkdtemp(prefix="songsmith-smoke-")
    wiki = os.path.join(tmp, "wiki")
    shutil.copytree(a.wiki, wiki)
    data = a.keep or os.path.join(tmp, "data")
    model = ScriptedModel(CALLS, fix_calls="follow").start()
    config = Config(os.path.join(data, "config.json"), {
        "model": {"base_url": model.url, "name": "scripted"},
        "famitracker": {"url": a.url, "token": a.token, "timeout_seconds": 900},
        "wiki_path": wiki, "limits": {"retries": 1, "wait_minutes": 10},
        "gates": {"review_score": 6, "yardstick_max_misses": 6}})
    store = Store(data)
    job = store.add({"name": a.name, "about": "A bright meadow stage with streams to jump.", "kind": "lively",
                     "chip": "VRC6", "length": "short"})[0]
    started = time.time()
    Runner(store, config).run_once()
    job = store.get(job["id"])
    model.stop()
    print("status:", job["status"], job.get("error", ""))
    print("song:", job.get("song"))
    print("ogg:", job.get("ogg_files"))
    print("seconds: %.0f, model calls %d, tool calls %d" % (time.time() - started, job["usage"]["calls"],
                                                            job["usage"]["tool_calls"]))
    for rec in job["phases"]:
        last = (rec.get("gates") or [[]])[-1]
        print("  %-16s %-16s tries %d  %s" % (rec["title"], rec["status"], len(rec.get("gates") or []),
                                              "; ".join("%s %s" % (g["name"], "ok" if g["ok"] else "FAILED: " +
                                                                    g["summary"]) for g in last)))
    print("notes:", json.dumps(job.get("notes"), indent=1))
    print("report:", os.path.join(store.job_dir(job["id"]), "report.md"))
    print("song log in the wiki copy:", job.get("song_log"))
    return 0 if job["status"] == "done" else 1


if __name__ == "__main__":
    sys.exit(main())

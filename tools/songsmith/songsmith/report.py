"""A job's report (report.md and report.json in its folder) and its song log for the wiki."""

import datetime
import json
import os
import re

from .config import save_json
from .phases import PHASES


def when(t):
    return datetime.datetime.fromtimestamp(t).strftime("%Y-%m-%d %H:%M") if t else "-"


def duration(seconds):
    seconds = int(seconds or 0)
    return "%dh %02dm" % (seconds // 3600, seconds // 60 % 60) if seconds >= 3600 else "%dm %02ds" % (
        seconds // 60, seconds % 60)


def gate_lines(job):
    out = []
    for rec in job.get("phases", []):
        attempts = rec.get("gates") or []
        if not attempts:
            continue
        last = attempts[-1]
        marks = ", ".join("%s %s" % (r["name"], "skipped" if r.get("skipped") else "ok" if r["ok"] else "FAILED")
                          for r in last)
        out.append("| %s | %s | %d | %s | %s |" % (rec["title"], rec.get("status", ""), len(attempts), marks,
                                                    "; ".join(r["summary"] for r in last).replace("|", "/")))
    return out


def markdown(job):
    u = job.get("usage", {})
    lines = [
        "# Songsmith report: %s" % job["title"], "",
        "- Status: **%s**%s" % (job["status"], (" (%s)" % job["error"]) if job.get("error") else ""),
        "- Kind: %s; flavour: %s; chip: %s; length: %s%s" % (
            job["kind"], job.get("flavour") or "none", job.get("chip"), job.get("length"),
            "; tempo hint: %s" % job["tempo"] if job.get("tempo") else ""),
        "- Options: %s" % ", ".join("%s %s" % (k, "on" if v else "off") for k, v in job.get("options", {}).items()),
        "- Song: %s" % ((job.get("song") or {}).get("file") or "-"),
        "- OGG: %s" % (", ".join(job.get("ogg_files") or job.get("ogg") or []) or "none"),
        "- Started %s, ended %s, %s of work" % (when(job.get("started")), when(job.get("ended")),
                                               duration(job.get("seconds"))),
        "- Tokens: %d prompt, %d completion in %d model calls; %d tool calls" % (
            u.get("prompt", 0), u.get("completion", 0), u.get("calls", 0), u.get("tool_calls", 0)),
        "", "## The map", "", job.get("about") or "-", "",
        "## Phases", "", "| Phase | Status | Tries | Gates | Last results |", "|---|---|---|---|---|",
    ]
    lines += gate_lines(job) or ["| - | - | - | - | - |"]
    for rec in job.get("phases", []):
        t = rec.get("tokens", {})
        lines += ["", "### %s" % rec["title"],
                  "%s, %s, %d tool calls, %d + %d tokens." % (rec.get("status"), duration(rec.get("seconds")),
                                                             rec.get("tool_calls", 0), t.get("prompt", 0),
                                                             t.get("completion", 0)), ""]
        if rec.get("summary"):
            lines += [rec["summary"].strip(), ""]
        for note in rec.get("notes", []):
            lines.append("- Note: " + note)
    if job.get("notes"):
        lines += ["", "## Notes", ""] + ["- " + n for n in job["notes"]]
    final = job.get("final") or {}
    if final:
        lines += ["", "## The final check lines", ""]
        for tool in ("check_song", "review_arrangement", "listen", "measure_mix", "finish_song"):
            if final.get(tool):
                lines += ["### %s" % tool, "", "```", final[tool].strip()[:3000], "```", ""]
    if job.get("verdicts"):
        lines += ["", "## Verdicts", ""] + ['- %s: "%s"' % (v["date"], v["words"]) for v in job["verdicts"]]
    return "\n".join(lines) + "\n"


def write(folder, job):
    os.makedirs(folder, exist_ok=True)
    save_json(os.path.join(folder, "report.json"), job)
    with open(os.path.join(folder, "report.md"), "w", encoding="utf-8") as f:
        f.write(markdown(job))


def hook_lines(job):
    """the hook's steps lines from the hook phase's summary (lines that look like steps)"""
    for rec in job.get("phases", []):
        if rec["id"] == "hook":
            found = [l.strip() for l in (rec.get("summary") or "").split("\n")
                     if re.search(r"[A-G][#b-]?\d", l) and (" - " in l or " . " in l or "|" in l)]
            return found[:4]
    return []


def plan_table(job):
    return [l for l in (job.get("plan") or "").split("\n") if l.strip().startswith("|")][:16]


def song_log(job):
    """the song log, in the wiki's Song log template's form"""
    song = job.get("song") or {}
    final = job.get("final") or {}
    yard = []
    for rec in job.get("phases", []):
        for attempt in (rec.get("gates") or [])[-1:]:
            for g in attempt:
                if g["name"] == "yardstick":
                    yard = [r for r in g.get("data", {}).get("rows", []) if not r["ok"]]
    shape = ""
    for line in (final.get("review_arrangement") or "").split("\n"):
        if line.startswith("Numbers:"):
            shape = line
    out = [
        "---", "tags: [famitracker-wiki, song-log]", "kind: %s" % job["kind"],
        "date: %s" % datetime.date.today().isoformat(), "route: guided", "verdict: none yet", "---",
        "# Song log: %s" % song.get("name", job["title"]), "",
        "- File: Songs/%s/%s, OGG: %s" % (song.get("name", ""), song.get("file", ""),
                                          ", ".join(job.get("ogg_files") or job.get("ogg") or []) or "none"),
        "- Kind: [[Kind - %s]]. Made by Songsmith (the harness, phase by phase) with %s." % (
            job["kind"], "the full tool set" if job.get("options", {}).get("full_tools") else "the small tool set"),
        "- Flavour: %s; chip: %s" % (job.get("flavour") or "none", job.get("chip")),
        "- The map: %s" % (job.get("about") or "-").replace("\n", " "),
    ]
    if shape:
        out.append("- " + shape)
    out += ["", "## Plan"] + (plan_table(job) or ["(no plan table)"])
    out += ["", "## The hook"] + (["    " + l for l in hook_lines(job)] or ["(see the report)"])
    out += ["", "## What the tools said"]
    for tool in ("check_song", "listen", "review_arrangement", "measure_mix"):
        if final.get(tool):
            first = " ".join(final[tool].strip().split("\n")[:3])[:400]
            out.append("- %s: %s" % (tool, first))
    out += ["", "## Against the yardstick"]
    out += ["- %s: %s (want %s)" % (r["check"], r["value"], r["target"]) for r in yard] or [
        "- [[Yardstick - the first 30 in numbers]]: every row the harness counts within range."]
    notes = job.get("notes") or []
    if notes:
        out += ["", "## Notes"] + ["- " + n for n in notes[-8:]]
    out += ["", "## Verdict", ""]
    return "\n".join(out) + "\n"


def phase_names():
    return [p["title"] for p in PHASES]

"""Gates between phases, run by the harness through MCP: each gives {name, ok, summary, detail, fixes}."""

import json
import re

from . import yardstick
from .song import parse_parts, parse_song, review_score

WORTH = ("high", "medium")


def gate(name, ok, summary, detail="", fixes=None, data=None, skipped=False):
    return {"name": name, "ok": bool(ok), "summary": summary, "detail": detail, "fixes": fixes or [],
            "data": data or {}, "skipped": skipped}


def call_text(call):
    return "%s %s" % (call.get("tool", ""), json.dumps(call.get("arguments", {}), ensure_ascii=False))


def check_song(mcp, kind, settings):
    """no finding worth fixing (high or medium) that still has a fix"""
    reply = mcp.call("check_song", {"format": "json", "kind": kind})
    if reply.is_error:
        return gate("check_song", True, "check_song couldn't run: " + reply.text[:200], skipped=True)
    try:
        data = json.loads(reply.text)
    except ValueError:
        return gate("check_song", True, "check_song's JSON couldn't be read", skipped=True)
    findings = data.get("findings", [])
    worth = [f for f in findings if f.get("severity") in WORTH and isinstance(f.get("fix"), dict)]
    counts = {k: v for k, v in (data.get("counts") or {}).items() if v}
    lines = []
    fixes = []
    for f in worth[:10]:
        fix = f["fix"]
        calls = [fix] + [t for t in fix.get("then", []) if isinstance(t, dict)]
        fixes.append(calls)
        message = (f.get("message") or f.get("summary") or "").split(" Fix: ")[0]
        lines.append("- [%s %s] %s Fix: %s" % (f.get("severity"), f.get("kind"), message,
                                                ", then ".join(call_text(c) for c in calls)))
    summary = "%d findings (%s); %d worth fixing with a fix" % (
        len(findings), ", ".join("%s %d" % kv for kv in counts.items()) or "none", len(worth))
    return gate("check_song", not worth, summary, "\n".join(lines), fixes, {"counts": counts, "worth": len(worth)})


def review(mcp, kind, settings):
    bar = float(settings["gates"]["review_score"])
    reply = mcp.call("review_arrangement", {"format": "short", "kind": kind})
    if reply.is_error:
        return gate("review", True, "review_arrangement couldn't run: " + reply.text[:200], skipped=True)
    score = review_score(reply.text)
    if score is None:
        return gate("review", True, "the review gave no score", reply.text[:1500], skipped=True)
    return gate("review", score >= bar, "review %s/10 (bar %s)" % (score, bar), reply.text[:2500],
                data={"score": score, "parts": parse_parts(reply.text), "text": reply.text})


def measure_mix(mcp, kind, settings):
    """the mix's sanity: the loudness in a sane range and no part (but a bass) well over the melody"""
    reply = mcp.call("measure_mix", {"format": "short"})
    if reply.is_error and "Unknown tool" in reply.text:
        return gate("measure_mix", True, "measure_mix isn't in FamiTracker's small set of tools: not measured",
                    skipped=True)
    if reply.is_error:
        reply = mcp.call("measure_mix", {})
    if reply.is_error:
        return gate("measure_mix", True, "measure_mix couldn't run: " + reply.text[:200], skipped=True)
    text = reply.text
    g = settings["gates"]
    problems = []
    match = re.search(r"(-?\d+(?:\.\d+)?) LUFS", text)
    lufs = float(match.group(1)) if match else None
    if lufs is not None and not (g["loudness_low"] <= lufs <= g["loudness_high"]):
        problems.append("the loudness %.1f LUFS is outside %s to %s" % (lufs, g["loudness_low"], g["loudness_high"]))
    for m in re.finditer(r"([A-Za-z0-9 #+]+?(?: \(([a-z ]+)\))?) (\d+(?:\.\d+)?) dB over it", text):
        part, role, db = m.group(1).strip(), (m.group(2) or ""), float(m.group(3))
        limit = g["mix_max_over_db"] + (2.0 if "bass" in role or "riangle" in part else 0.0)
        if db > limit:
            problems.append("%s is %.1f dB over the melody" % (part, db))
    summary = ("mix: " + ("; ".join(problems) if problems else "sane") +
               (" (%.1f LUFS)" % lufs if lufs is not None else ""))
    return gate("measure_mix", not problems, summary, text[:2000], data={"lufs": lufs})


def yardstick_gate(mcp, kind, settings, job=None, wiki=None, parts=None):
    parts = parts or {}
    try:
        rows, song = yardstick.measure(mcp, kind, parts.get("melody"), parts.get("bass"),
                                       drums_wanted=(job or {}).get("options", {}).get("drums", True), wiki=wiki)
    except RuntimeError as error:
        return gate("yardstick", True, "the yardstick couldn't be counted: %s" % error, skipped=True)
    if not rows:
        return gate("yardstick", True, "the yardstick: nothing to count yet", skipped=True)
    misses = [r for r in rows if not r["ok"]]
    most = int(settings["gates"]["yardstick_max_misses"])
    return gate("yardstick", len(misses) <= most, "yardstick: %d of %d rows under (at most %d)" % (
        len(misses), len(rows), most), yardstick.rows_text(rows), data={"rows": rows})


def song_ready(mcp, kind, settings, job=None, wants=None):
    """after the start: sections, a chosen key; after the sounds: instruments; after the hook: a melody"""
    song = parse_song(mcp.text("get_song", {}))
    problems = []
    wants = wants or []
    if "sections" in wants and not song["sections"]:
        problems.append("no sections yet (song_shape lays them out)")
    if "key" in wants and not song["key_chosen"]:
        problems.append("no key chosen (set_key)")
    if "sounds" in wants and song["instruments"] < 4:
        problems.append("only %d instruments (design or find the sounds of the plan)" % song["instruments"])
    if "notes" in wants and not any(c["notes"] for c in song["channels"]):
        problems.append("no notes yet")
    if "parts" in wants:
        playing = sum(1 for c in song["channels"] if c["notes"])
        if playing < min(4, len(song["channels"])):
            problems.append("only %d channels play" % playing)
    return gate("song", not problems, "; ".join(problems) or "the song has what this phase makes",
                data={"song": song})


def run(names, mcp, kind, settings, job=None, wiki=None, wants=None):
    """the gates named, in order; the review's parts are passed to the yardstick"""
    out = []
    parts = None
    for name in names:
        if name == "check_song":
            out.append(check_song(mcp, kind, settings))
        elif name == "review":
            result = review(mcp, kind, settings)
            parts = result["data"].get("parts")
            out.append(result)
        elif name == "measure_mix":
            out.append(measure_mix(mcp, kind, settings))
        elif name == "yardstick":
            if parts is None:
                reply = mcp.call("review_arrangement", {"format": "short", "kind": kind})
                parts = parse_parts(reply.text) if not reply.is_error else {}
            out.append(yardstick_gate(mcp, kind, settings, job, wiki, parts))
        elif name == "song":
            out.append(song_ready(mcp, kind, settings, job, wants))
    return out


def failures_text(results):
    out = []
    for r in results:
        if not r["ok"]:
            out.append("## %s: %s\n%s" % (r["name"], r["summary"], r["detail"]))
    return "\n\n".join(out)

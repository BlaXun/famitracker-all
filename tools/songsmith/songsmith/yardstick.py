"""The yardstick numbers the harness can count itself from get_song and read_notes (wiki: "Yardstick - the first 30
in numbers"), counted as the wiki's own scripts count them: a bar's rhythm is where its notes start."""

import statistics

from .song import is_drums, parse_song, parse_steps, pitch, pitch_name

SOFT_KINDS = {"calm", "cozy", "mysterious", "floating", "melancholic", "atmospheric", "bittersweet"}
CHUNK = 16      # bars a read_notes call

# The wiki's row for each check (its first cell starts so); its "When under" cell is the advice
WIKI_ROWS = {
    "parts": "Parts at once", "instruments": "Instruments in the song", "rhythms": "Different bar rhythms",
    "repeats": "Bars whose rhythm came earlier", "hook": "The hook's two-bar rhythm", "register": "Register",
    "bass": "Bass: different bar rhythms", "changes": "Channels changing", "ending": "Parts in the loop's last",
    "noise": "The noise in a soft song",
}
FALLBACK = {
    "parts": "fill every channel of the chip with a part that has a job: a second line, a chord part, soft ticks",
    "instruments": "a new sound for B and another for the climax; a bell or a sound of the place for the break",
    "rhythms": "keep the hook's rhythm where a section starts; give the bars between rhythms of their own",
    "repeats": "over 0.7: vary the bars between the hook's returns",
    "hook": "over 5: it's a typed pattern, not a hook; keep it for section starts",
    "register": "above E-4: transpose the melody semitones -12, or write it an octave lower",
    "bass": "another bass_style per section",
    "changes": "at each new section change the parts already playing, not only add one",
    "ending": "keep the band going into the loop (drums in the last bar)",
    "noise": "add_drums groove calm or lively, intensity light",
}


def wiki_advice(wiki, key):
    text = wiki.text("Yardstick - the first 30 in numbers") if wiki else None
    label = WIKI_ROWS[key]
    for line in (text or "").split("\n"):
        if line.startswith("| " + label):
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if cells:
                return cells[-1]
    return FALLBACK[key]


def read_all(mcp, channels, bars):
    """every channel's steps, bar by bar"""
    out = {}
    for ch in channels:
        steps, changes = {}, []
        if ch["notes"]:
            for start in range(1, bars + 1, CHUNK):
                reply = mcp.call("read_notes", {"channel": ch["name"], "from_bar": start,
                                                "to_bar": min(bars, start + CHUNK - 1), "format": "steps"})
                if reply.is_error:
                    continue
                part, change = parse_steps(reply.text)
                steps.update(part)
                changes += change
        out[ch["name"]] = (steps, changes)
    return out


def rhythm(tokens):
    return tuple(i for i, t in enumerate(tokens) if t not in ("-", "."))


def measure(mcp, kind, melody=None, bass=None, drums_wanted=True, wiki=None, song_text=None):
    """the rows: [{key, check, value, target, ok, advice}], and the song's overview"""
    song = parse_song(song_text or mcp.text("get_song", {}))
    bars = song["bars"]
    channels = song["channels"]
    nch = max(1, len(channels))
    if not bars or not channels:
        return [], song
    body = [b for b in range(1, bars + 1)]
    intro = [s for s in song["sections"] if s[0].lower().startswith("intro")]
    if intro:
        body = [b for b in body if not any(s[1] <= b <= s[2] for s in intro)]
    notes = read_all(mcp, channels, bars)

    def starts(name, bar):
        return rhythm(notes.get(name, ({}, []))[0].get(bar, []))

    rows = []

    def row(key, check, value, target, ok):
        rows.append({"key": key, "check": check, "value": value, "target": target, "ok": bool(ok),
                     "advice": "" if ok else wiki_advice(wiki, key)})

    names = [c["name"] for c in channels]
    # texture
    per_bar = [sum(1 for n in names if starts(n, b)) for b in body]
    parts = round(statistics.mean(per_bar), 2) if per_bar else 0
    want = round(0.62 * nch, 1)
    row("parts", "Parts at once (main sections)", parts, ">= %.1f of %d channels" % (want, nch), parts >= want)
    least = 9 if kind in SOFT_KINDS else 12
    row("instruments", "Instruments in the song", song["instruments"], ">= %d" % least, song["instruments"] >= least)

    # the melody
    melody = melody if melody in names else max(
        [n for n in names if not is_drums(n)] or names, key=lambda n: sum(len(starts(n, b)) for b in body))
    rhs = [starts(melody, b) for b in body]
    filled = [r for r in rhs if r]
    if filled:
        distinct = round(len(set(filled)) / len(filled) * 8, 2)
        row("rhythms", "Melody: different bar rhythms per 8 bars", distinct, ">= 2.6 (first 30: 3.5)", distinct >= 2.6)
        seen, again = set(), 0
        for r in filled:
            again += r in seen
            seen.add(r)
        share = round(again / len(filled), 2)
        row("repeats", "Melody: bars whose rhythm came earlier", share, "<= 0.70 (first 30: 0.56)", share <= 0.70)
        sections = [s for s in song["sections"] if not s[0].lower().startswith("intro")]
        first = sections[0][1] if sections else body[0]
        hook = (starts(melody, first), starts(melody, first + 1))
        if any(hook):
            pairs = sum(1 for i, b in enumerate(body[:-1]) if (rhs[i], rhs[i + 1]) == hook)
            per32 = round(pairs * 32 / max(1, len(body)), 2)
            row("hook", "The hook's two-bar rhythm back, per 32 bars", per32, "<= 5 (first 30: 2.0)", per32 <= 5)
    pitches = [p for b in body for t in notes.get(melody, ({}, []))[0].get(b, []) for p in [pitch(t)] if p is not None]
    if pitches:
        middle = int(statistics.median(pitches))
        row("register", "Melody's middle note (written)", pitch_name(middle), "G3 to E4 (first 30: B3)", 67 <= middle <= 76)

    # the bass
    if bass not in names:
        melodic = [n for n in names if n != melody and not is_drums(n) and notes[n][0]]

        def low(n):
            ps = [pitch(t) for b in body for t in notes[n][0].get(b, []) if pitch(t) is not None]
            return statistics.median(ps) if ps else 999
        bass = min(melodic, key=low) if melodic else None
    if bass:
        brh = [r for r in (starts(bass, b) for b in body) if r]
        if brh:
            value = round(len(set(brh)) / len(brh) * 8, 2)
            row("bass", "Bass: different bar rhythms per 8 bars", value, ">= 0.8 (first 30: 1.11)", value >= 0.8)

    # channels changing per 8-bar block
    blocks = [body[i] for i in range(0, len(body), 8)]
    if len(blocks) >= 2:
        def signature(n, bar):
            steps, changes = notes.get(n, ({}, []))
            tokens = steps.get(bar, [])
            ps = [pitch(t) for t in tokens if pitch(t) is not None]
            octave = int(statistics.median(ps)) // 12 if ps else None
            sound = None
            for at, name in changes:
                if at <= bar:
                    sound = name
            return (bool(rhythm(tokens)), rhythm(tokens), octave, sound)
        counts = [sum(1 for n in names if signature(n, b) != signature(n, a)) for a, b in zip(blocks, blocks[1:])]
        value = round(statistics.mean(counts), 2)
        want = round(0.6 * nch, 1)
        row("changes", "Channels changing per 8-bar block", value, ">= %.1f of %d (first 30: 6.7 of 8)" % (want, nch),
            value >= want)

    # the loop's last two bars
    last = [bars - 1, bars]
    playing = sum(1 for n in names if any(notes[n][0].get(b, []) and any(t != "." for t in notes[n][0].get(b, []))
                                          for b in last))
    drums = [n for n in names if is_drums(n)]
    drum_last = any(starts(n, bars) for n in drums)
    want = max(3, round(0.5 * nch))
    ok = playing >= want and (drum_last or not drums_wanted or not any(notes[n][0] for n in drums))
    row("ending", "Parts in the last 2 bars", "%d%s" % (playing, ", drums in the last bar" if drum_last else ""),
        ">= %d, drums in the last bar" % want, ok)

    if kind in SOFT_KINDS and drums_wanted:
        noise = [n for n in names if "noise" in n.lower()]
        if noise:
            share = round(sum(1 for b in body if starts(noise[0], b)) / len(body), 2)
            row("noise", "The noise in a soft song (share of bars)", share, ">= 0.5 (first 30: 0.57-1.0)", share >= 0.5)
    return rows, song


def rows_text(rows):
    out = []
    for r in rows:
        mark = "ok" if r["ok"] else "UNDER"
        line = "- %s: %s (want %s) %s" % (r["check"], r["value"], r["target"], mark)
        if not r["ok"]:
            line += ". When under: " + r["advice"]
        out.append(line)
    return "\n".join(out)

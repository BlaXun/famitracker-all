"""Reading FamiTracker's replies: get_song's overview, read_notes' steps, the review's parts."""

import re

NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def pitch(token):
    """a steps token's pitch as a MIDI number (FamiTracker's octaves: A-3 is 440 Hz, 69), None for drums and holds"""
    match = re.match(r"^([A-G])([#b-]?)(-?\d)", token)
    if not match:
        return None
    value = NOTE[match.group(1)] + {"#": 1, "b": -1}.get(match.group(2), 0)
    return value + 12 * (int(match.group(3)) + 2)


def pitch_name(value):
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    return names[value % 12] + str(value // 12 - 2)


def parse_song(text):
    """get_song's overview as data"""
    out = {"title": "", "file": "", "bars": 0, "sections": [], "channels": [], "instruments": 0, "key": "",
           "key_chosen": False, "tempo": "", "rows_per_bar": 16}
    lines = text.split("\n")
    if lines:
        match = re.match(r'^"(.*)"(?:, file (.+))?$', lines[0].strip())
        if match:
            out["title"] = match.group(1)
            out["file"] = (match.group(2) or "").strip()
    for line in lines:
        s = line.strip()
        if s.startswith("Length:"):
            match = re.search(r"(\d+) bars", s)
            out["bars"] = int(match.group(1)) if match else 0
        elif s.startswith("Tempo:"):
            out["tempo"] = s[len("Tempo:"):].strip()
            match = re.search(r"\((\d+) rows\)", s)
            if match:
                out["rows_per_bar"] = int(match.group(1))
        elif s.startswith("Sections:"):
            for m in re.finditer(r'"([^"]+)" bars? (\d+)(?:-(\d+))?', s):
                out["sections"].append((m.group(1), int(m.group(2)), int(m.group(3) or m.group(2))))
        elif s.startswith("Key:"):
            out["key"] = s[4:].split(";")[0].split("(")[0].strip()
            out["key_chosen"] = "chosen for this track" in s
        elif s.startswith("Instruments:"):
            out["instruments"] = len(re.findall(r'\b[0-9A-F]{2} "', s))
        else:
            match = re.match(r"^\s*(\d+) ([^:]+?): (.*)$", line)
            if match and line.startswith("  "):
                count = re.match(r"(\d+) (?:notes|hits)", match.group(3))
                out["channels"].append({"number": int(match.group(1)), "name": match.group(2).strip(),
                                        "notes": int(count.group(1)) if count else 0})
    if not out["instruments"]:
        match = re.search(r"with (\d+) sounds", text)
        out["instruments"] = int(match.group(1)) if match else 0
    return out


def parse_steps(text):
    """read_notes' steps: {bar: [tokens]} and the instrument changes [(bar, name)]"""
    bars = {}
    changes = []
    for line in text.split("\n"):
        match = re.match(r"^(\d+):\s*(.*)$", line.strip())
        if match:
            tokens = [t for t in match.group(2).split() if t != "|"]
            bars[int(match.group(1))] = tokens
        elif line.startswith("Instrument"):
            first = re.match(r'^Instruments?:?\s*"([^"]+)"', line)
            if first:
                changes.append((1, first.group(1)))
            for m in re.finditer(r'from bar (\d+)[^"]*"([^"]+)"', line):
                changes.append((int(m.group(1)), m.group(2)))
    return bars, changes


def parse_parts(review):
    """the review's "Parts:" line: the melody's and the bass's channels"""
    out = {"melody": None, "bass": None, "chords": None}
    match = re.search(r"Parts: (.*)", review)
    if not match:
        return out
    line = match.group(1)
    for role in ("melody", "bass", "chords"):
        m = re.search(role + r" on ([^;(]+?)(?: \(|;|$)", line)
        if m:
            out[role] = m.group(1).strip().rstrip(".")
    return out


def review_score(text):
    match = re.search(r"(\d+(?:\.\d+)?)/10", text or "")
    return float(match.group(1)) if match else None


def is_drums(name):
    return "noise" in name.lower() or "dpcm" in name.lower()

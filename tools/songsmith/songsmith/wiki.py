"""The FamiTracker wiki (an LLM wiki in the user's vault): pages and sections read verbatim, the pages a phase needs
(AGENTS.md's reading order, Librarian.md's "where to look"), search for the model's own lookups, and filing back
(song logs, verdicts) exactly as AGENTS.md says. Read fresh for every job: the user keeps changing it."""

import datetime
import os
import re

# The pages a working wiki has; the check warns when one is missing
NEEDED = ["AGENTS.md", "index.md", "Craft moves.md", "Yardstick - the first 30 in numbers.md", "Loops and seams.md",
          "Verdicts.md", "Song log template.md", "Flavours.md", "How to compose a song here.md", "Mix by kind.md"]

# AGENTS.md's direct lookups: the cookbook of each kind (used when the kind page doesn't link one)
COOKBOOK = {
    "calm": "slow", "floating": "slow", "bittersweet": "slow", "melancholic": "slow", "mysterious": "slow",
    "atmospheric": "slow", "cozy": "walking", "tense": "walking", "playful": "walking", "lively": "fast",
    "adventure": "fast", "festive": "fast", "action": "fast",
}

SEARCH_PAGES = 8        # pages a search lists at most
SEARCH_LINES = 3        # lines shown a page
LINE_CHARS = 220
READ_CHARS = 9000       # a page read whole, at most (else: its headings, to read a section)


def strip_front_matter(text):
    if text.startswith("---"):
        end = text.find("\n---", 3)
        if end >= 0:
            return text[end + 4:].lstrip("\r\n")
    return text


def plain_links(text):
    """[[Page#part|words]] as words, [[Page]] as Page"""
    return re.sub(r"\[\[([^\]|]+)\|([^\]]+)\]\]", r"\2", re.sub(r"\[\[([^\]|]+)\]\]", r"\1", text))


def links_in(text):
    """the pages a text links, in order (without #parts)"""
    out = []
    for match in re.finditer(r"\[\[([^\]|#]+)", text):
        name = match.group(1).strip()
        if name not in out:
            out.append(name)
    return out


class Wiki:
    def __init__(self, path):
        self.path = path
        self.pages = {}     # name without .md (as linked, with folders) -> file path
        self.reload()

    def reload(self):
        self.pages = {}
        if not os.path.isdir(self.path):
            return
        for folder, dirs, files in os.walk(self.path):
            dirs[:] = [d for d in dirs if not d.startswith(".")]
            for name in files:
                if name.endswith(".md"):
                    full = os.path.join(folder, name)
                    rel = os.path.relpath(full, self.path)[:-3].replace(os.sep, "/")
                    self.pages[rel] = full

    def exists(self):
        return os.path.isdir(self.path) and bool(self.pages)

    # ---- finding and reading
    def find(self, name):
        """a page by its name, its path in the wiki or its last part, any case, with or without .md"""
        if not name:
            return None
        key = name.strip().replace("\\", "/")
        if key.lower().endswith(".md"):
            key = key[:-3]
        key = key.split("#")[0].split("|")[0].strip()
        if key in self.pages:
            return key
        lower = key.lower()
        for page in self.pages:
            if page.lower() == lower:
                return page
        for page in self.pages:
            if page.split("/")[-1].lower() == lower.split("/")[-1]:
                return page
        return None

    def raw_text(self, page):
        found = self.find(page)
        if not found:
            return None
        with open(self.pages[found], encoding="utf-8", errors="replace") as f:
            return f.read().replace("\r\n", "\n")

    def text(self, page):
        """a page as written, without its front matter"""
        raw = self.raw_text(page)
        return None if raw is None else strip_front_matter(raw)

    def headings(self, page):
        text = self.text(page) or ""
        return [line.strip() for line in text.split("\n") if line.startswith("#")]

    def section(self, page, heading):
        """the section under a heading (`## 3. ...` by "3", "3." or its words; `## cozy` by "cozy"), verbatim,
        up to the next heading as high or higher; None when there's none"""
        text = self.text(page)
        if text is None:
            return None
        want = heading.strip().lstrip("#").strip().lower().rstrip(".")
        lines = text.split("\n")
        start = level = None
        for i, line in enumerate(lines):
            if not line.startswith("#"):
                continue
            title = line.lstrip("#").strip().lower()
            number = title.split(".")[0] if re.match(r"^\d+\.", title) else None
            if title == want or number == want or title.startswith(want + ".") or (len(want) > 3 and title.startswith(want)):
                start, level = i, len(line) - len(line.lstrip("#"))
                break
        if start is None:
            return None
        end = len(lines)
        for i in range(start + 1, len(lines)):
            if lines[i].startswith("#") and len(lines[i]) - len(lines[i].lstrip("#")) <= level:
                end = i
                break
        return "\n".join(lines[start:end]).strip()

    def read(self, page, heading=None, limit=READ_CHARS):
        """the wiki_read tool: a page or one section, verbatim and bounded"""
        found = self.find(page)
        if not found:
            near = self.search(page, pages=5, lines=0)
            return "No page \"%s\". %s" % (page, near if near else "index.md lists the pages.")
        if heading:
            part = self.section(found, heading)
            if part is None:
                return "No section \"%s\" on %s. Its headings: %s" % (heading, found, "; ".join(self.headings(found)))
            text = part
        else:
            text = self.text(found)
        if len(text) > limit:
            text = text[:limit] + "\n[... cut at %d characters. Read one section: %s]" % (
                limit, "; ".join(h for h in self.headings(found) if h.startswith("##")))
        return "%s\n\n(Page: %s)" % (text, found)

    def search(self, words, pages=SEARCH_PAGES, lines=SEARCH_LINES):
        """the wiki_search tool: pages whose lines have the words, the best first, with a few matching lines.
        raw/ is left out (AGENTS.md: raw pages only for a pattern, never to copy)"""
        terms = [w for w in re.findall(r"[\w#'-]+", (words or "").lower()) if len(w) > 1]
        if not terms:
            return "Give some words to search for."
        scored = []
        for page, path in self.pages.items():
            if page.startswith("raw/"):
                continue
            text = self.text(page) or ""
            lower = text.lower()
            name = page.lower()
            score = sum(3 for t in terms if t in name) + sum(min(lower.count(t), 5) for t in terms)
            have = sum(1 for t in terms if t in lower or t in name)
            if have == 0:
                continue
            score += 4 * have
            hits = []
            for line in text.split("\n"):
                low = line.lower()
                n = sum(1 for t in terms if t in low)
                if n:
                    hits.append((n, line.strip()[:LINE_CHARS]))
            hits.sort(key=lambda h: -h[0])
            scored.append((score, page, [h[1] for h in hits[:lines]]))
        if not scored:
            return "Not in the wiki."
        scored.sort(key=lambda s: (-s[0], s[1]))
        out = []
        for _, page, hits in scored[:pages]:
            out.append("- " + page + "".join("\n    " + h for h in hits))
        return "\n".join(out) + "\nRead one with wiki_read (page, and a heading for one section)."

    # ---- the pages a song needs
    def kind_page(self, kind):
        return self.find("Kind - " + kind)

    def examples(self, kind):
        """the examples the kind page names (its **Example:** line first), in order"""
        text = self.text("Kind - " + kind) or ""
        out = []
        for line in text.split("\n"):
            if "Example" in line and line.lstrip().startswith("**Example"):
                out += [p for p in links_in(line) if p.startswith("Example") and self.find(p)]
        for page in links_in(text):
            if page.startswith("Example") and self.find(page) and page not in out:
                out.append(page)
        return out

    def example_for(self, kind, version=1):
        names = self.examples(kind)
        if not names:
            return None
        return names[(max(1, version) - 1) % len(names)]

    def cookbook(self, kind):
        """the kind's section of its hook cookbook, verbatim (page, text)"""
        text = self.text("Kind - " + kind) or ""
        match = re.search(r"\[\[(Hook cookbook - [^#\]|]+)", text)
        page = match.group(1) if match else "Hook cookbook - " + COOKBOOK.get(kind, "fast")
        found = self.find(page)
        return (found, self.section(found, kind) if found else None)

    def flavour_rows(self, flavour):
        """the Flavours table's header and the flavour's row, and lines about it below"""
        text = self.text("Flavours") or ""
        lines = text.split("\n")
        header = [l for l in lines if l.startswith("| Flavour")][:1]
        sep = [l for l in lines if l.startswith("|---")][:1]
        rows = [l for l in lines if l.lower().startswith("| " + flavour.lower() + " ")]
        notes = [l for l in lines if l.lower().startswith("**" + flavour.lower())]
        if not rows:
            return None
        return "\n".join(header + sep + rows + ([""] + notes if notes else []))

    def verdicts_for(self, kind, limit=3500):
        """the rows of Verdicts.md's table that concern the kind (its word, its examples' songs) and the newest
        general ones; the user's ear wins over any page"""
        text = self.text("Verdicts") or ""
        lines = text.split("\n")
        table = [l for l in lines if l.startswith("|")]
        if len(table) < 3:
            return None
        header, rows = table[:2], table[2:]
        songs = [e.replace("Example - ", "").split(" (")[0].lower() for e in self.examples(kind)]
        words = [kind.lower()] + [s for s in songs if len(s) > 3]
        mine = [r for r in rows if any(re.search(r"\b" + re.escape(w) + r"\b", r.lower()) for w in words)]
        general = [r for r in rows if r not in mine][:4]
        chosen = [r for r in rows if r in mine or r in general]
        out = "\n".join(header + chosen)
        while len(out) > limit and len(chosen) > 1:
            chosen = chosen[:-1]
            out = "\n".join(header + chosen)
        return out

    def check(self, kinds):
        """what a test of the settings shows: the pages found, the ones missing"""
        found = sorted(self.pages)
        missing = [p[:-3] for p in NEEDED if not self.find(p[:-3])]
        missing_kinds = [k for k in kinds if not self.kind_page(k)]
        warnings = []
        if not os.path.isdir(self.path):
            warnings.append("The wiki folder isn't there: %s" % self.path)
        if "AGENTS.md" in [m + ".md" for m in missing]:
            warnings.append("AGENTS.md is missing: the wiki's reading order can't be followed.")
        if missing_kinds:
            warnings.append("Kind pages missing: %s" % ", ".join(missing_kinds))
        if missing:
            warnings.append("Pages missing: %s" % ", ".join(missing))
        return {"path": self.path, "pages": len(found),
                "kinds": [k for k in kinds if k not in missing_kinds], "missing": missing,
                "missing_kinds": missing_kinds, "warnings": warnings,
                "examples": sorted(p for p in found if p.startswith("Example"))}

    # ---- filing back (AGENTS.md, "Filing back after a song"); nothing else is ever written
    def file_path(self, rel):
        return os.path.join(self.path, *rel.split("/"))

    def song_log_page(self, song):
        return "Song logs/" + song

    def file_song_log(self, song, text, kind, route="guided", date=None):
        """Song logs/<song>.md (a new name when taken) and its line in log.md; the page's name"""
        date = date or today()
        page = self.song_log_page(safe_name(song))
        n = 2
        while os.path.exists(self.file_path(page) + ".md"):
            page = self.song_log_page("%s (%d)" % (safe_name(song), n))
            n += 1
        write_text(self.file_path(page) + ".md", text)
        append_line(self.file_path("log.md"), "- %s song: [[%s]] (%s, %s)" % (date, page, kind, route))
        self.reload()
        return page

    def file_verdict(self, song, words, log_page=None, date=None):
        """the file-verdict skill's steps: Verdicts verbatim, a row at the top of Verdicts, the song log, log.md"""
        date = date or today()
        changed = []
        verbatim = self.file_path("raw/verdicts/Verdicts verbatim.md")
        append_block(verbatim, "## %s\n- On \"%s\": \"%s\"" % (date, song, words))
        changed.append("Verdicts verbatim")
        log_page = log_page if log_page and self.find(log_page) else (
            self.song_log_page(song) if self.find(self.song_log_page(song)) else None)
        link = "[[%s]]" % log_page if log_page else song
        verdicts = self.file_path("Verdicts.md")
        if os.path.exists(verdicts):
            raw, newline = read_keep(verdicts)
            lines = raw.split("\n")
            for i, line in enumerate(lines):
                if line.startswith("|---"):
                    lines.insert(i + 1, "| %s | %s | \"%s\" | to be compiled | %s |" % (date, song, words.replace("|", "/"), link))
                    break
            write_text(verdicts, "\n".join(lines), newline)
            changed.append("Verdicts")
        if log_page:
            path = self.pages.get(self.find(log_page))
            raw, newline = read_keep(path)
            raw = re.sub(r"(?m)^verdict:.*$", "verdict: \"%s\"" % words.replace("\"", "'"), raw, count=1)
            if "## Verdict" in raw:
                raw = raw.rstrip("\n") + "\n%s: \"%s\"\n" % (date, words)
            else:
                raw = raw.rstrip("\n") + "\n\n## Verdict\n%s: \"%s\"\n" % (date, words)
            write_text(path, raw, newline)
            changed.append("song log")
        append_line(self.file_path("log.md"), "- %s verdict: %s" % (date, song))
        changed.append("log")
        self.reload()
        return changed


def today():
    return datetime.date.today().isoformat()


def safe_name(name):
    return re.sub(r"[\\/:*?\"<>|]", "-", name).strip() or "Untitled"


def read_keep(path):
    """a file's text with \\n line ends, and the line end it had"""
    with open(path, encoding="utf-8", errors="replace", newline="") as f:
        raw = f.read()
    newline = "\r\n" if "\r\n" in raw else "\n"
    return raw.replace("\r\n", "\n"), newline


def write_text(path, text, newline="\n"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".songsmith-tmp"
    with open(tmp, "w", encoding="utf-8", newline="") as f:
        f.write(text.replace("\r\n", "\n").replace("\n", newline))
    os.replace(tmp, path)


def append_line(path, line):
    if os.path.exists(path):
        raw, newline = read_keep(path)
    else:
        raw, newline = "", "\n"
    if raw and not raw.endswith("\n"):
        raw += "\n"
    write_text(path, raw + line + "\n", newline)


def append_block(path, block):
    if os.path.exists(path):
        raw, newline = read_keep(path)
    else:
        raw, newline = "", "\n"
    raw = raw.rstrip("\n")
    write_text(path, (raw + "\n\n" if raw else "") + block + "\n", newline)

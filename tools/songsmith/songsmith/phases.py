"""The eight phases of a song, as the wiki's one-shot prompt runs them: what each asks of the model, the wiki pages it
gets (AGENTS.md's reading order, Librarian.md's "where to look"), the tools it may call and the gates after it."""

from .config import CHIPS, LENGTHS

# Tools the harness keeps for itself: the song's file, its backups and other songs are the harness's business
HARNESS_ONLY = {"new_song", "open_song", "save_song", "create_backup", "list_backups", "restore_backup", "select_track",
                "set_expansion", "send_file", "import_nsf", "study_nsf", "study_module", "set_mixer", "check_exercise",
                "soundtrack_notes", "make_version", "keep_version", "compare_versions", "make_jingle",
                "make_sound_effect", "look_at_song", "play", "stop", "place_clip"}

READ = ["get_song", "read_notes", "write_log", "explain", "what_next"]

PHASES = [
    {"id": "plan", "title": "Plan",
     "tools": ["get_song", "get_recipe", "explain", "write_log"],
     "pages": ["index", "How to compose a song here", "kind", "example", "Craft moves", "flavour", "verdicts"],
     "gates": ["plan"]},
    {"id": "start", "title": "Start",
     "tools": READ + ["set_tempo", "set_key", "song_shape", "name_section", "set_loop", "set_meter", "get_recipe"],
     "pages": ["kind", "Loops and seams#The intro", "verdicts"],
     "gates": ["song"], "wants": ["sections", "key"]},
    {"id": "sounds", "title": "Sounds",
     "tools": READ + ["design_instrument", "find_sound", "add_instrument", "reference_sound", "set_instrument"],
     "pages": ["kind", "Sound traps in short", "flavour", "verdicts"],
     "gates": ["song"], "wants": ["sounds"]},
    {"id": "hook", "title": "Hook and melody",
     "tools": READ + ["grow_melody", "write_notes", "transpose", "copy_bars", "vary_part", "check_song", "undo"],
     "pages": ["cookbook", "Hooks - motif, repeat, answer", "Craft moves#1", "Craft moves#6", "Craft moves#11",
               "verdicts"],
     "gates": ["song", "check_song"], "wants": ["notes"]},
    {"id": "parts", "title": "Parts",
     "tools": READ + ["add_chords_and_bass", "add_drums", "add_ambience", "add_harmony", "add_counter_melody",
                      "add_echo", "write_notes", "copy_bars", "design_instrument", "find_sound", "set_instrument",
                      "vary_part", "add_sample_drums", "make_groove", "check_song", "undo"],
     "pages": ["kind", "Chords that change", "Craft moves#2", "Craft moves#4", "Craft moves#5", "Craft moves#10",
               "verdicts"],
     "gates": ["song", "check_song"], "wants": ["parts"]},
    {"id": "development", "title": "Development",
     "tools": READ + ["vary_part", "write_notes", "copy_bars", "polish", "set_swing", "transpose", "change_mood",
                      "set_instrument", "design_instrument", "find_sound", "add_vibrato", "add_harmony",
                      "add_counter_melody", "add_echo", "add_chords_and_bass", "add_drums", "set_pan",
                      "check_song", "review_arrangement", "undo"],
     "pages": ["Yardstick - the first 30 in numbers", "Development - sections that change", "Craft moves#3",
               "Craft moves#7", "Craft moves#8", "Craft moves#9", "verdicts"],
     "gates": ["check_song", "review", "yardstick"]},
    {"id": "checks", "title": "Checks",
     "tools": READ + ["check_song", "review_arrangement", "listen", "measure_mix", "polish", "mix", "check_loop",
                      "fix_loop", "write_notes", "vary_part", "set_instrument", "add_vibrato", "copy_bars", "undo"],
     "pages": ["Yardstick - the first 30 in numbers", "Sound traps in short", "Mix by kind", "Loops and seams",
               "verdicts"],
     "gates": ["check_song", "review", "measure_mix", "yardstick"]},
    {"id": "finish", "title": "Finish",
     "tools": READ + ["finish_song", "check_song", "listen", "polish", "fix_loop", "check_loop", "write_notes",
                      "vary_part", "export_for_game", "undo"],
     "pages": ["Loops and seams", "Mix by kind", "flavour", "verdicts"],
     "gates": ["check_song", "export"]},
]

PHASE_IDS = [p["id"] for p in PHASES]

HARNESS_TOOLS = [
    {"type": "function", "function": {
        "name": "phase_done",
        "description": "Call this when this phase's work is done: a short summary of what you did and what later "
                       "phases need (names of sounds, channels, bars). In the plan phase, the whole plan.",
        "parameters": {"type": "object", "properties": {"summary": {"type": "string"}}, "required": ["summary"]}}},
    {"type": "function", "function": {
        "name": "wiki_search",
        "description": "Search the FamiTracker wiki (the composing knowledge: kinds, craft moves, the yardstick, hooks, "
                       "verdicts) for words: the pages that have them, with a few matching lines.",
        "parameters": {"type": "object", "properties": {"words": {"type": "string"}}, "required": ["words"]}}},
    {"type": "function", "function": {
        "name": "wiki_read",
        "description": "Read a wiki page verbatim, or one section of it by its heading (\"7\" for Craft moves' move 7, "
                       "\"cozy\" for a cookbook's section).",
        "parameters": {"type": "object", "properties": {"page": {"type": "string"}, "heading": {"type": "string"}},
                       "required": ["page"]}}},
]

CHANNELS = {
    "": ["2A03 Pulse 1", "2A03 Pulse 2", "2A03 Triangle", "2A03 Noise", "2A03 DPCM"],
    "VRC6": ["VRC6 Pulse 1", "VRC6 Pulse 2", "VRC6 Sawtooth"],
}


def option_lines(job):
    o = job.get("options", {})
    out = []
    out.append("Drums: yes (drums or soft ticks in most bars)." if o.get("drums", True) else
               "Drums: none. Leave out add_drums; the noise plays ambience or nothing.")
    if o.get("ambience"):
        out.append("Ambience: yes, the place's own sound (add_ambience), quiet, on the noise where it plays no drums.")
    else:
        out.append("Ambience: none.")
    if o.get("swing"):
        out.append("Swing: a light swing (set_swing light) in the parts of the song that suit it.")
    out.append("Intro: yes, 2-4 bars at A's pulse, ending with a pickup." if o.get("intro", True) else
               "Intro: none; the song starts with A (song_shape drop intro).")
    return out


def brief(job):
    """the song as the user asked for it"""
    lines = ["Name: %s" % job["title"], "The map: %s" % (job.get("about") or "(no description)"),
             "Kind: %s" % job["kind"], "Flavour: %s" % (job.get("flavour") or "none"),
             "Chip: %s" % job.get("chip", "VRC6"),
             "Length: %s (about %d bars)" % (job.get("length", "normal"), LENGTHS.get(job.get("length"), 40))]
    if job.get("tempo"):
        lines.append("Tempo hint from the user: %s" % job["tempo"])
    lines += option_lines(job)
    if job.get("versions", 1) > 1:
        lines.append("This is version %d of %d of this song: make it your own (another form variant, other sounds, "
                     "another hook), not a copy of the others." % (job.get("version", 1), job["versions"]))
    return "\n".join("- " + l for l in lines)


def calls_args(job):
    """the kind, flavour and about to pass to every call that takes them"""
    args = {"kind": job["kind"]}
    if job.get("flavour"):
        args["flavour"] = job["flavour"]
    if job.get("about"):
        args["about"] = job["about"][:300]
    return args


def task(phase, job, extra=""):
    """the phase's request to the model"""
    import json
    kind = job["kind"]
    args = calls_args(job)
    a = json.dumps(args, ensure_ascii=False)
    fk = json.dumps({k: v for k, v in args.items() if k != "about"}, ensure_ascii=False)
    bars = LENGTHS.get(job.get("length"), 40)
    pid = phase["id"]
    if pid == "plan":
        text = (
            "Plan the song before any notes. The song is open in FamiTracker (empty); get_song shows its channels.\n"
            "1. Choose the key, the tempo in BPM, a form with bars (about %d bars; vary it: song_shape takes variant, "
            "add, drop or parts), the sounds (the kind's lead word, a second line, chords, bass, a new sound for B "
            "and another for the climax).\n"
            "2. Write the plan table: a row for every channel of the chip, a column for every section, saying what "
            "the channel plays there and which new sound comes in where. Aim for 5-7 parts at once in the main "
            "sections, 10 or more instruments, drums or soft ticks in most bars (unless the song has none), and "
            "sections that change most of their parts. Mark the highlight.\n"
            "3. Say the hook's idea in words (its rhythm and shape).\n"
            "4. write_log with category \"plan\" and the table, then phase_done with the whole plan (key, tempo, "
            "form, sounds, table, hook idea) as its summary." % bars)
    elif pid == "start":
        intro = "" if job.get("options", {}).get("intro", True) else ", with drop [\"intro\"]"
        text = (
            "Set up the song's frame from your plan:\n"
            "1. set_tempo with bpm (the kind page's tempos; the user's hint wins), set_key.\n"
            "2. song_shape with %s, bars %d%s (or parts as your plan's list): it lays out the sections. If the plan's "
            "form differs, pass parts.\n"
            "3. get_song to check the sections and the loop; set_loop or name_section only where needed.\n"
            "what_next %s is an advisor here. Then phase_done." % (a, bars, intro, a))
    elif pid == "sounds":
        text = (
            "Make the sounds your plan names, before any notes: the lead with design_instrument (the kind's lead word "
            "as its kind, a vibrato on the lead; on its channel), the second line, the chords, the bass, a new sound "
            "for B and another for the climax; find_sound for ready-made ones. Read each reply: no sound may vanish "
            "within 4 frames for a tune, and long vibrato notes go on a VRC6 pulse rather than the 2A03's. "
            "Give them names you'll recognize. Then phase_done with the sounds by name and channel.")
    elif pid == "hook":
        text = (
            "The melody, yourself:\n"
            "1. Write your own hook: a 2-bar motif as a line of steps, 4-7 notes a bar (a token an eighth, - holds, "
            ". rests, | between bars), its middle around B3 written, high points in octave 4. The cookbook's hooks "
            "are patterns: change the key and some pitches; never copy a kept song's tune.\n"
            "2. grow_melody with that motif on the lead's channel over the first main section (A), kind %s; pick "
            "the version you would hum (grow_melody's pick).\n"
            "3. Every other section of the melody: the hook returns at section starts, the bars between vary their "
            "rhythm; B gets its own rhythm (its own motif to grow_melody, or write_notes in steps), the climax a "
            "higher peak; the intro a pickup in its last bar that holds a chord tone into the downbeat; a break "
            "rests. Read write_notes' warnings and fix them.\n"
            "4. read_notes with format steps to see it; check_song for clashes. Then phase_done with the hook's "
            "steps line and the melody's channel." % kind)
    elif pid == "parts":
        drums = ("add_drums per section with a groove and an intensity (lighter in A, busier in the climax), "
                 if job.get("options", {}).get("drums", True) else "no drums, ")
        amb = "add_ambience (quiet, where the noise plays no drums), " if job.get("options", {}).get("ambience") else ""
        text = (
            "Write the other parts, section by section, as your plan says:\n"
            "- add_chords_and_bass per section (from_bar and to_bar, or section), another chord_style and "
            "bass_style per section: lighter in A, busier in B and the climax; chords that change on the half bar "
            "at cadences and one or two from outside the key in each section; other chords under the repeat of A.\n"
            "- %s%s"
            "a second line that answers in the tune's rests (add_counter_melody with rests answer, or write_notes "
            "on a free channel), the echo from A' (add_echo), a harmony in the climax (add_harmony).\n"
            "- every channel of the chip has a job in the main sections; one bass per section.\n"
            "Read each reply's warnings. check_song for clashes, then phase_done with what plays where." % (drums, amb))
    elif pid == "development":
        swing = " set_swing light where it suits." if job.get("options", {}).get("swing") else ""
        text = (
            "Development: make the sections differ as the first 30 do (Yardstick and Craft moves). Compare the song "
            "with your plan: does each section bring something in (a part, a sound, a figure)?\n"
            "- At each section border change most of the parts already playing (another figure, sound or octave), "
            "not only add one; vary_part on repeats (restrike, octave_up, syncopate, embellish).\n"
            "- A lead with dynamics: not every note at one volume; the band steps back where the tune sings.\n"
            "- Lead-ins hold a chord tone into the downbeat; the last bars keep the band playing into the loop.\n"
            "- Techniques at the first 30's rates: polish touches (fades_under, duty, scoops; staccato for "
            "action).%s\n"
            "Use review_arrangement %s and check_song as your ear. Then phase_done." % (swing, fk))
    elif pid == "checks":
        text = (
            "Check the song: check_song %s and fix every finding that has a fix (details that may be meant can "
            "stay); listen once (it renders the real audio) and fix what it heard that has a call; "
            "review_arrangement %s, aiming for 8/10 or more: follow the tips that serve the plan; measure_mix, "
            "and fix single parts against the kind's mix row (mix with the kind's style for band kinds, or levels). "
            "Leave the mixer's faders at 0 dB. check_loop and fix_loop for the seam. Then phase_done with the final "
            "check lines." % (fk, fk))
    else:
        text = (
            "Finish: finish_song %s. Fix what it heard (each Heard: line gives its call) and call finish_song again "
            "until it's clean. Don't change volumes after it; if you change notes after it, run it again. It exports "
            "the OGG. Then phase_done with its final lines (check_song, listen, the file)." % fk)
    if extra:
        text += "\n\n" + extra
    return text


def fix_task(phase, job, failures, attempt, retries):
    return ("The harness checked the song after this phase and found what to fix (try %d of %d):\n\n%s\n\n"
            "Fix these now with the tools (each finding gives its call), then phase_done with what you changed."
            % (attempt, retries, failures))


def system_prompt(phase, number, job, state, pages, tools_note=""):
    """the phase's system message: the role, the rules, the song, the state so far and the wiki pages it needs"""
    out = [
        "You are composing one NES-style song for BlaXun's cozy online MMO platformer, in FamiTracker, through its "
        "MCP tools. You are the composer; the tools are your instruments. The quality bar is his first 30 soundtrack "
        "songs.",
        "You work in phases. This is phase %d of %d: %s. Do only this phase's work, then call phase_done with a short "
        "summary. A harness runs the phases: it saves the song and makes backups, so you never save, open or start "
        "songs yourself." % (number, len(PHASES), phase["title"]),
        "Rules: the user's verdicts (Verdicts below) win over any tool or page. Never copy a kept song's tune or an "
        "example's hook. Pass the same kind and flavour to every call that takes them. Steps notation: a token an "
        "eighth, - holds, . rests, | between bars; FamiTracker's octaves (A-3 is 440 Hz). Every song: 0 clicks, "
        "0 vanishing notes. Leave the mixer's faders at 0 dB.",
        "The wiki: the pages below are what this phase needs. wiki_search and wiki_read look up anything else "
        "(a craft move, a yardstick row, an example) verbatim.",
        "", "# The song", brief(job), "", "# Where the song stands", state or "(nothing done yet)",
    ]
    if tools_note:
        out += ["", tools_note]
    if pages:
        out += ["", "# Wiki pages for this phase (verbatim)"]
        for title, text in pages:
            out += ["", "## [%s]" % title, text]
    return "\n".join(out)


def page_list(phase, job, wiki):
    """the phase's pages from the wiki: (title, text) in reading order, each found fresh"""
    kind = job["kind"]
    out = []
    for name in phase["pages"]:
        if name == "kind":
            page = wiki.kind_page(kind)
            text = wiki.text(page) if page else None
            title = page
        elif name == "example":
            page = wiki.example_for(kind, job.get("version", 1))
            text = wiki.text(page) if page else None
            title = page
        elif name == "cookbook":
            title, text = wiki.cookbook(kind)
            title = "%s, section %s" % (title, kind) if title else None
        elif name == "flavour":
            if not job.get("flavour"):
                continue
            text = wiki.flavour_rows(job["flavour"])
            title = "Flavours, %s" % job["flavour"]
        elif name == "verdicts":
            text = wiki.verdicts_for(kind)
            title = "Verdicts that concern %s songs (the user's ear wins)" % kind
        elif "#" in name:
            page, heading = name.split("#", 1)
            text = wiki.section(page, heading)
            title = "%s, %s" % (page, (text or "").split("\n")[0].lstrip("# ") or heading)
        else:
            text = wiki.text(name)
            title = name
        if text and title:
            out.append((title, text.strip()))
    return out


def tools_for(phase, job, available):
    """the FamiTracker tools this phase offers (as tools/list gave them), less what the options leave out"""
    names = [n for n in phase["tools"] if n in available and n not in HARNESS_ONLY]
    o = job.get("options", {})
    if not o.get("drums", True):
        names = [n for n in names if n not in ("add_drums", "add_sample_drums", "make_groove")]
    if not o.get("ambience"):
        names = [n for n in names if n != "add_ambience"]
    if not o.get("swing"):
        names = [n for n in names if n != "set_swing"]
    return names


def expansion(job):
    return CHIPS.get(job.get("chip", "VRC6"), ["VRC6"])

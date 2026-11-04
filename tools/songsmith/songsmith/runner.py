"""The worker: one job at a time, phase after phase, each a short agent loop of the model with FamiTracker's tools,
with the harness's own gates between them."""

import json
import os
import threading
import time
import traceback

from . import gates as gatelib
from . import phases as ph
from . import report
from .llm import ModelClient, strip_thinking, tool_schema
from .mcp import McpClient
from .net import AuthError, Patience, Stopped, Unreachable
from .song import parse_song
from .wiki import Wiki


class SetupProblem(Exception):
    """something every job needs is missing (the wiki): the queue pauses rather than failing job after job"""


class Runner:
    def __init__(self, store, config, mcp_factory=None, model_factory=None, sleep=time.sleep):
        self.store = store
        self.config = config
        self.mcp_factory = mcp_factory or self.make_mcp
        self.model_factory = model_factory or self.make_model
        self.sleep = sleep
        self.quit = threading.Event()
        self.thread = None
        self.current = None     # the job running now

    # ---- clients
    def make_mcp(self, settings, job, patience):
        f = settings["famitracker"]
        return McpClient(f["url"], f["token"], core=not job["options"].get("full_tools"),
                         timeout=f["timeout_seconds"], patience=patience)

    def make_model(self, settings, patience):
        m = settings["model"]
        return ModelClient(m["base_url"], m["name"], m.get("api_key", ""), m["timeout_seconds"], m["temperature"],
                           m["max_tokens"], m.get("extra"), patience)

    # ---- the loop
    def start(self):
        self.thread = threading.Thread(target=self.loop, name="songsmith-runner", daemon=True)
        self.thread.start()

    def stop(self, wait=30):
        self.quit.set()
        if self.thread:
            self.thread.join(wait)

    def loop(self):
        while not self.quit.is_set():
            job = self.store.next_job()
            if not job:
                self.quit.wait(2)
                continue
            self.run_job(job)

    def run_once(self):
        """run the next job to its end (for tests and the command line); False when there's none"""
        job = self.store.next_job()
        if not job:
            return False
        self.run_job(job)
        return True

    # ---- a job
    def run_job(self, job):
        store = self.store
        settings = self.config.get()
        self.current = job
        job["status"] = "running"
        job["control"] = None
        job["error"] = ""
        if not job.get("started"):
            job["started"] = time.time()
        self.job_started = time.time()
        store.save(job)
        store.event(job, "job", "Started%s." % (" again at phase %d" % (job["phase"] + 1) if job["phase"] else ""))
        patience = Patience(settings["limits"]["wait_minutes"], lambda: self.should_stop(job),
                            lambda text: store.event(job, "wait", text), self.sleep)
        ctx = JobContext(self, job, settings, patience)
        try:
            ctx.prepare()
            while job["phase"] < len(ph.PHASES):
                if self.should_stop(job):
                    raise Stopped()
                ctx.run_phase(job["phase"])
                job["phase"] += 1
                store.save(job)
            ctx.finish_job()
            job["status"] = "done"
            store.event(job, "job", "Done.")
        except Stopped:
            control = job.get("control")
            if control in ("cancel", "skip"):
                job["status"] = "cancelled" if control == "cancel" else "skipped"
                store.event(job, "job", "%s during phase %d." % (job["status"].capitalize(), job["phase"] + 1))
            else:
                job["status"] = "waiting"
                job["resume"] = True
                store.event(job, "job", "Paused at phase %d; it resumes there." % (job["phase"] + 1))
            ctx.leave_saved()
        except (AuthError, Unreachable, SetupProblem) as error:
            # FamiTracker, the model or the wiki can't be had: every job would fail alike, so the job waits and the
            # queue pauses until the user resumes it
            job["status"] = "waiting"
            job["error"] = str(error)
            job["resume"] = job.get("song") is not None
            store.event(job, "error", "Stopped: %s. The queue is paused; resume it once that's fixed (the job goes "
                                      "on at phase %d)." % (error, job["phase"] + 1))
            store.set_paused(True, "Paused by Songsmith: %s" % error)
            ctx.leave_saved()
        except Exception as error:     # a bug or an answer nobody foresaw: the job fails, the queue goes on
            job["status"] = "failed"
            job["error"] = "%s: %s" % (type(error).__name__, error)
            job["resume"] = True
            store.event(job, "error", "Failed: %s" % traceback.format_exc()[-3000:])
            ctx.leave_saved()
        finally:
            job["seconds"] = round(job.get("seconds", 0) + time.time() - self.job_started)
            if job["status"] in ("done", "failed", "cancelled", "skipped"):
                job["ended"] = time.time()
            job["control"] = None
            store.save(job)
            try:
                report.write(store.job_dir(job["id"]), job)
            except Exception:
                store.event(job, "error", "The report couldn't be written: %s" % traceback.format_exc()[-800:])
            self.current = None

    def should_stop(self, job):
        return self.quit.is_set() or job.get("control") in ("cancel", "skip") or self.store.paused()


class JobContext:
    """what one job's run needs: the clients, the wiki read fresh, the tools of FamiTracker"""

    def __init__(self, runner, job, settings, patience):
        self.runner = runner
        self.store = runner.store
        self.job = job
        self.settings = settings
        self.patience = patience
        self.limits = settings["limits"]
        self.mcp = None
        self.model = None
        self.wiki = None
        self.tools = {}

    def log(self, kind, text, data=None):
        self.store.event(self.job, kind, text, data)

    # ---- before the phases
    def prepare(self):
        job = self.job
        self.wiki = Wiki(self.settings["wiki_path"])     # fresh for every job: the user keeps changing it
        if not self.wiki.exists():
            raise SetupProblem("The wiki isn't at %s (Settings: wiki path)." % self.settings["wiki_path"])
        check = self.wiki.check([job["kind"]])
        for warning in check["warnings"]:
            self.store.note(job, "Wiki: " + warning)
        self.log("wiki", "Wiki read: %d pages at %s." % (check["pages"], check["path"]))
        self.mcp = self.runner.mcp_factory(self.settings, job, self.patience)
        self.model = self.runner.model_factory(self.settings, self.patience)
        self.tools = {t["name"]: t for t in self.mcp.list_tools()}
        self.log("connect", "FamiTracker: %d tools on %s." % (len(self.tools), self.mcp.route))
        if job.get("song") is None:
            self.new_song()
        else:
            self.reopen_song()

    def unsaved(self, reply):
        text = reply.text.lower()
        return reply.is_error and ("not saved" in text or "unsaved" in text or "aren't saved" in text
                                   or "changes that" in text)

    def make_room(self, what):
        """the open song has changes not saved: save them (the setting "save") or wait for the user ("wait")"""
        if self.settings.get("unsaved_song") == "wait":
            self.log("wait", "The song open in FamiTracker has changes not saved; waiting for them to be saved.")
            self.patience.pause(60)
            return
        reply = self.mcp.call("save_song", {})
        if reply.is_error:
            reply = self.mcp.call("save_song", {"name": "Songsmith rescued %s" % time.strftime("%Y-%m-%d %H%M")})
        self.store.note(self.job, "Saved the song that was open in FamiTracker with changes, before %s: %s"
                        % (what, reply.text[:200]))

    def new_song(self):
        job = self.job
        args = {"name": job["title"], "expansion": ph.expansion(job)}
        for attempt in range(30):
            reply = self.mcp.call("new_song", args)
            if not self.unsaved(reply):
                break
            self.make_room("the new song")
        if reply.is_error:
            raise RuntimeError("new_song failed: %s" % reply.text[:300])
        song = parse_song(self.mcp.text("get_song", {}))
        name = song["file"][:-4] if song["file"].lower().endswith(".ftm") else (song["file"] or job["title"])
        job["song"] = {"file": song["file"], "name": name, "title": song["title"]}
        self.store.save(job)
        self.log("song", "New song \"%s\" (%s): %s" % (name, ", ".join(args["expansion"]) or "2A03", reply.text[:300]))

    def reopen_song(self):
        """after a restart, a pause or a failure: the job's song open again, as it was when the phase began"""
        job = self.job
        song = parse_song(self.mcp.text("get_song", {}))
        if song["file"] != job["song"]["file"]:
            for attempt in range(30):
                reply = self.mcp.call("open_song", {"name": job["song"]["name"]})
                if not self.unsaved(reply):
                    break
                self.make_room("opening the job's song again")
            if reply.is_error:
                raise RuntimeError("open_song \"%s\" failed: %s" % (job["song"]["name"], reply.text[:300]))
            self.log("song", "Opened \"%s\" again." % job["song"]["name"])
        record = self.record(job["phase"]) if job["phase"] < len(ph.PHASES) else None
        if record and record.get("backup"):
            reply = self.mcp.call("restore_backup", {"backup": record["backup"]})
            self.log("song", "Back to the backup \"%s\": %s" % (record["backup"], reply.text[:200]))
            self.save()
        job["resume"] = False

    def leave_saved(self):
        """after a stop: the song saved, so FamiTracker has nothing unsaved and the next job can start"""
        if self.mcp and self.job.get("song"):
            try:
                self.save()
            except Exception:
                pass

    def save(self):
        reply = self.mcp.call("save_song", {})
        if reply.is_error:
            self.store.note(self.job, "save_song: " + reply.text[:300])
        return reply

    # ---- phases
    def record(self, index):
        job = self.job
        while len(job["phases"]) <= index:
            p = ph.PHASES[len(job["phases"])]
            job["phases"].append({"id": p["id"], "title": p["title"], "status": "waiting", "attempts": 0,
                                  "gates": [], "summary": "", "tokens": {"prompt": 0, "completion": 0},
                                  "tool_calls": 0, "seconds": 0, "notes": []})
        return job["phases"][index]

    def run_phase(self, index):
        job = self.job
        phase = ph.PHASES[index]
        rec = self.record(index)
        rec["status"] = "running"
        rec["started"] = time.time()
        rec["gates"] = []
        rec["attempts"] = 0
        self.store.save(job)
        self.log("phase", "Phase %d of %d: %s." % (index + 1, len(ph.PHASES), phase["title"]),
                 {"phase": index})
        label = "songsmith %s %s" % (job["id"], phase["id"])
        reply = self.mcp.call("create_backup", {"label": label})
        rec["backup"] = label if not reply.is_error else None
        if reply.is_error:
            self.store.note(job, "create_backup before %s: %s" % (phase["id"], reply.text[:200]))
        self.store.save(job)
        started = time.time()
        late = self.job_late()
        extra = self.phase_extra(phase) if not late else ""
        retries = int(self.limits["retries"])
        if late and phase["id"] != "finish":
            self.store.note(job, "The job's time is up: phase %s skipped." % phase["title"])
            rec["status"] = "skipped"
            rec["seconds"] = round(time.time() - started)
            self.store.save(job)
            return
        summary = self.agent(phase, index, ph.task(phase, job, extra), rec)
        rec["summary"] = summary
        while True:
            self.save()
            results = self.run_gates(phase)
            rec["gates"].append(results)
            self.store.save(job)
            failed = [r for r in results if not r["ok"]]
            for r in results:
                self.log("gate", "%s %s: %s" % ("skipped" if r.get("skipped") else "passed" if r["ok"] else "FAILED",
                                                r["name"], r["summary"]),
                         {"ok": r["ok"], "name": r["name"], "detail": r["detail"][:3000]})
            if not failed or rec["attempts"] >= retries or self.job_late():
                break
            rec["attempts"] += 1
            also = {c.get("tool") for r in failed for calls in r["fixes"] for c in calls}
            more = self.agent(phase, index, ph.fix_task(phase, job, gatelib.failures_text(results), rec["attempts"],
                                                         retries), rec, also)
            rec["summary"] = (rec["summary"] + "\nFixes: " + more)[:2000]
        if failed and self.settings["gates"].get("auto_fix"):
            fixed = self.auto_fix(results)
            if fixed:
                self.save()
                results = self.run_gates(phase)
                rec["gates"].append(results)
                failed = [r for r in results if not r["ok"]]
        if failed:
            note = "Phase %s: gates still failing after %d tries: %s" % (
                phase["title"], rec["attempts"] + 1, "; ".join("%s (%s)" % (r["name"], r["summary"]) for r in failed))
            rec["notes"].append(note)
            self.store.note(job, note)
        if phase["id"] == "plan":
            job["plan"] = job.get("plan_draft") or summary
            reply = self.mcp.call("write_log", {"category": "plan", "text": job["plan"][:6000]})
            if reply.is_error:
                self.store.note(job, "write_log of the plan: " + reply.text[:200])
        self.save()
        rec["status"] = "done" if not failed else "done with notes"
        rec["seconds"] = round(time.time() - started)
        rec["ended"] = time.time()
        self.store.save(job)

    def job_late(self):
        return time.time() - self.runner.job_started + self.job.get("seconds", 0) > self.limits["job_hours"] * 3600

    def phase_extra(self, phase):
        """what the harness measured, for the phases that use it"""
        if phase["id"] == "development":
            g = gatelib.yardstick_gate(self.mcp, self.job["kind"], self.settings, self.job, self.wiki,
                                       self.parts())
            if g["detail"]:
                return "The harness counted the yardstick on the song as it is:\n" + g["detail"]
        if phase["id"] == "checks":
            results = gatelib.run(["check_song", "review"], self.mcp, self.job["kind"], self.settings, self.job,
                                  self.wiki)
            return "The harness's checks now:\n" + "\n".join("- %s" % r["summary"] for r in results)
        return ""

    def parts(self):
        reply = self.mcp.call("review_arrangement", {"format": "short", "kind": self.job["kind"]})
        from .song import parse_parts
        return parse_parts(reply.text) if not reply.is_error else {}

    def run_gates(self, phase):
        out = []
        names = phase["gates"]
        for name in names:
            if name == "plan":
                plan = self.job.get("plan_draft", "")
                rows = [l for l in plan.split("\n") if l.strip().startswith("|")]
                out.append(gatelib.gate("plan", len(rows) >= 3, "the plan has a table (%d rows)" % len(rows) if rows
                                        else "the plan has no table",
                                        "" if len(rows) >= 3 else "Write the plan table: a row for every channel, a "
                                        "column for every section, then phase_done with the whole plan."))
            elif name == "export":
                if not self.job.get("ogg"):
                    self.export_myself()
                ok = bool(self.job.get("ogg"))
                out.append(gatelib.gate("export", ok, "the OGG: %s" % (", ".join(self.job["ogg"]) or "none"),
                                        "" if ok else "No OGG was exported: call finish_song %s." % json.dumps(
                                            {"kind": self.job["kind"]})))
            else:
                out += gatelib.run([name], self.mcp, self.job["kind"], self.settings, self.job, self.wiki,
                                   phase.get("wants"))
        return out

    def export_myself(self):
        reply = self.mcp.call("export_for_game", {})
        self.keep_files(reply)
        self.log("tool", "export_for_game (by the harness): %s" % reply.text[:500])

    def keep_files(self, reply):
        if reply.is_error:
            return
        for name in reply.files():
            if name not in self.job["ogg"]:
                self.job["ogg"].append(name)

    def auto_fix(self, results):
        """the fixes check_song gave, made by the harness itself after the retries; True when one was made"""
        done = False
        for r in results:
            if r["name"] != "check_song":
                continue
            for calls in r["fixes"][:6]:
                for call in calls:
                    tool = call.get("tool")
                    if not tool or tool in ph.HARNESS_ONLY or tool not in self.tools:
                        continue
                    reply = self.mcp.call(tool, call.get("arguments") or {})
                    done = True
                    self.log("tool", "auto-fix %s: %s" % (gatelib.call_text(call), reply.text[:300]))
                    if reply.is_error:
                        break
        if done:
            self.store.note(self.job, "The harness made check_song's fixes itself after the retries.")
        return done

    # ---- the agent loop
    def state_note(self, index):
        job = self.job
        out = []
        if job.get("plan") and index > 0:
            out.append("The plan (yours, from phase 1):\n" + job["plan"][:4000])
        for rec in job["phases"][:index]:
            if rec.get("summary") and rec["id"] != "plan":
                out.append("Phase %s: %s" % (rec["title"], rec["summary"][:700]))
        if index > 0:
            reply = self.mcp.call("get_song", {})
            if not reply.is_error:
                lines = [l[:400] for l in reply.text.split("\n")[:16]]
                out.append("The song now (get_song):\n" + "\n".join(lines))
        return "\n\n".join(out)

    def agent(self, phase, index, request, rec, also=()):
        """one short agent loop: the model calls tools until phase_done; returns its summary. also: tools a gate's
        fixes call, offered besides the phase's own"""
        job = self.job
        names = ph.tools_for(phase, job, self.tools)
        names += [n for n in sorted(t for t in also if t) if n in self.tools and n not in names and
                  n not in ph.HARNESS_ONLY]
        tools = [tool_schema(self.tools[n]) for n in names] + ph.HARNESS_TOOLS
        allowed = set(names) | {"phase_done", "wiki_search", "wiki_read"}
        pages = ph.page_list(phase, job, self.wiki)
        system = ph.system_prompt(phase, index + 1, job, self.state_note(index), pages,
                                  "Tools this phase: %s, and wiki_search, wiki_read, phase_done." % ", ".join(names))
        messages = [{"role": "system", "content": system}, {"role": "user", "content": request}]
        self.log("ask", request, {"pages": [t for t, _ in pages], "tools": names})
        deadline = time.time() + self.limits["phase_minutes"] * 60
        nudged = False
        summary = None
        last_text = ""
        for turn in range(int(self.limits["max_turns_per_phase"])):
            if self.runner.should_stop(job):
                raise Stopped()
            if time.time() > deadline:
                self.store.note(job, "Phase %s: its time (%d min) was up." % (phase["title"],
                                                                              self.limits["phase_minutes"]))
                break
            self.compact(messages)
            message, usage = self.model.chat(messages, tools)
            self.count(usage, rec)
            text = strip_thinking(message.get("content") or "")
            calls = message.get("tool_calls") or []
            if text:
                last_text = text
                self.log("model", text[:3000])
            entry = {"role": "assistant", "content": message.get("content") or ""}
            if calls:
                entry["tool_calls"] = calls
            messages.append(entry)
            if not calls:
                if phase["id"] == "plan" and text:
                    summary = text
                    break
                if nudged:
                    summary = text
                    break
                nudged = True
                messages.append({"role": "user", "content": "Go on with the tools, or call phase_done with your "
                                                            "summary if this phase is done."})
                continue
            for call in calls:
                fn = call.get("function") or {}
                name = fn.get("name", "")
                raw = fn.get("arguments") or "{}"
                try:
                    args = json.loads(raw) if isinstance(raw, str) else dict(raw)
                    if not isinstance(args, dict):
                        raise ValueError("not an object")
                except ValueError as error:
                    result = "Error: the arguments aren't a JSON object (%s): %s" % (error, str(raw)[:200])
                    args = None
                if args is not None:
                    result = self.tool(name, args, allowed, rec)
                    if name == "phase_done":
                        summary = str(args.get("summary") or "")
                self.store.event(job, "call", "%s %s" % (name, json.dumps(args, ensure_ascii=False)[:1500]
                                                         if args is not None else raw[:300]))
                self.store.event(job, "result", result[:3000], {"tool": name})
                messages.append({"role": "tool", "tool_call_id": call.get("id", ""), "name": name,
                                 "content": result[: int(self.limits["tool_result_chars"])]})
            if summary is not None:
                break
        if summary is None:
            summary = last_text or "(no summary)"
            self.store.note(job, "Phase %s ended without phase_done." % phase["title"])
        if phase["id"] == "plan":
            job["plan_draft"] = summary
        self.store.save(job)
        return summary[:6000]

    def tool(self, name, args, allowed, rec):
        """one call the model made: the harness's own, or FamiTracker's"""
        rec["tool_calls"] += 1
        self.job["usage"]["tool_calls"] += 1
        if name == "phase_done":
            return "Phase done. The harness checks the song now."
        if name == "wiki_search":
            return self.wiki.search(str(args.get("words", "")))
        if name == "wiki_read":
            return self.wiki.read(str(args.get("page", "")), args.get("heading") or None)
        if name not in allowed:
            if name in self.tools:
                return ("Error: %s isn't offered in this phase (the harness does saving, backups and songs itself). "
                        "Tools here: %s." % (name, ", ".join(sorted(allowed))))
            return "Error: no tool %s. Tools here: %s." % (name, ", ".join(sorted(allowed)))
        reply = self.mcp.call(name, args)
        if name in ("finish_song", "export_for_game"):
            self.keep_files(reply)
            self.job["final"][name] = reply.text[-3000:]
        if name in ("check_song", "review_arrangement", "listen", "measure_mix"):
            self.job["final"][name] = reply.text[:3000]
        return ("[isError] " if reply.is_error else "") + (reply.text or "(no text)")

    def count(self, usage, rec):
        p = int(usage.get("prompt_tokens") or 0)
        c = int(usage.get("completion_tokens") or 0)
        rec["tokens"]["prompt"] += p
        rec["tokens"]["completion"] += c
        self.job["usage"]["prompt"] += p
        self.job["usage"]["completion"] += c
        self.job["usage"]["calls"] += 1

    def compact(self, messages):
        """keep the context small: older tool replies shortened first, then dropped to a line"""
        budget = int(self.limits["context_chars"])

        def size():
            return sum(len(m.get("content") or "") + len(json.dumps(m.get("tool_calls", ""))) for m in messages)
        if size() <= budget:
            return
        for keep in (600, 120):
            for m in messages[2:-4]:
                if m["role"] == "tool" and len(m["content"]) > keep:
                    m["content"] = m["content"][:keep] + " [shortened]"
                if size() <= budget:
                    return

    # ---- after the phases
    def finish_job(self):
        job = self.job
        folder = self.store.job_dir(job["id"])
        final = job.setdefault("final", {})
        for tool, args in (("check_song", {"format": "short", "kind": job["kind"]}),
                           ("review_arrangement", {"format": "short", "kind": job["kind"]})):
            reply = self.mcp.call(tool, args)
            final[tool] = reply.text[:3000]
        names = list(job.get("ogg") or [])
        if not names:
            base = job["song"]["name"].lower()
            names = [n for n in self.mcp.list_files() if n.lower().startswith(base)]
        got = []
        for name in names:
            if self.mcp.download(name, os.path.join(folder, os.path.basename(name))):
                got.append(os.path.basename(name))
        job["ogg_files"] = got
        if names and not got:
            self.store.note(job, "The OGG couldn't be fetched from FamiTracker's /files/ route: %s" % ", ".join(names))
        self.save()
        if self.settings.get("file_song_logs"):
            try:
                page = self.wiki.file_song_log(job["song"]["name"], report.song_log(job), job["kind"], "guided")
                job["song_log"] = page
                self.log("wiki", "Filed the song log: %s, and a line in log.md." % page)
            except OSError as error:
                self.store.note(job, "The song log couldn't be filed: %s" % error)

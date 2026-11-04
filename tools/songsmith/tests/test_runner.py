import json
import os
import time
import unittest

from helpers import FORM, HarnessCase, slurp
from fakes import finding
from songsmith.config import save_json
from songsmith.phases import PHASES


class RunTests(HarnessCase):
    def run_job(self, form=None):
        jobs = self.store.add(dict(form or FORM))
        self.runner.run_once()
        return self.store.get(jobs[0]["id"])

    def test_whole_song(self):
        job = self.run_job()
        self.assertEqual(job["status"], "done", job.get("error"))
        self.assertEqual(job["phase"], len(PHASES))
        self.assertEqual([p["id"] for p in job["phases"]], [p["id"] for p in PHASES])
        names = self.ft.names()
        # the harness starts the song, backs up before each phase and saves after it
        self.assertIn(("new_song", {"name": "Songsmith test Meadow", "expansion": ["VRC6"]}), self.ft.calls)
        self.assertEqual(sum(1 for n in names if n == "create_backup"), len(PHASES))
        self.assertGreaterEqual(sum(1 for n in names if n == "save_song"), len(PHASES))
        self.assertIn("finish_song", names)
        self.assertFalse(self.ft.dirty)        # nothing left unsaved in FamiTracker
        # the OGG came through /files/ into the job's folder
        folder = self.store.job_dir(job["id"])
        self.assertEqual(job["ogg_files"], ["Songsmith test Meadow.ogg"])
        self.assertTrue(slurp(os.path.join(folder, "Songsmith test Meadow.ogg"), "rb").startswith(b"OggS"))
        report = slurp(os.path.join(folder, "report.md"))
        self.assertIn("Status: **done**", report)
        self.assertIn("| Finish |", report)
        self.assertIn("Tokens:", report)
        data = json.loads(slurp(os.path.join(folder, "report.json")))
        self.assertGreater(data["usage"]["prompt"], 0)
        self.assertIn("|", data["plan"])
        # the song log filed back into the wiki, and its line in log.md
        self.assertEqual(job["song_log"], "Song logs/Songsmith test Meadow")
        log = slurp(os.path.join(self.wiki, "Song logs", "Songsmith test Meadow.md"))
        self.assertIn("kind: lively", log)
        self.assertIn("| VRC6 Pulse 1 | pickup | hook |", log)
        self.assertIn("[[Song logs/Songsmith test Meadow]] (lively, guided)", slurp(os.path.join(self.wiki, "log.md")))

    def test_phases_get_their_pages_and_tools(self):
        self.run_job()
        by_phase = {}
        for body in self.model.requests:
            system = body["messages"][0]["content"]
            title = system.split("This is phase ")[1].split(":")[1].split(".")[0].strip()
            by_phase.setdefault(title, body)
        plan = by_phase["Plan"]["messages"][0]["content"]
        self.assertIn("## [Kind - lively]", plan)
        self.assertIn("## [Example - Test Song]", plan)
        self.assertIn("## [Craft moves]", plan)
        self.assertIn("Verdicts that concern lively songs", plan)
        hook = by_phase["Hook and melody"]["messages"][0]["content"]
        self.assertIn("F1: `C4 - E4 G4 - . A4 -`", hook)
        self.assertIn("Do move 1", hook)
        self.assertNotIn("Do move 2:", hook)
        self.assertIn("Phase Sounds:", hook)              # the state note carries earlier phases
        self.assertIn("The plan (yours", hook)
        finish = by_phase["Finish"]["messages"][0]["content"]
        self.assertIn("## [Loops and seams]", finish)
        tools = {b["function"]["name"] for b in by_phase["Finish"]["tools"]}
        self.assertIn("finish_song", tools)
        self.assertIn("wiki_search", tools)
        self.assertNotIn("save_song", tools)
        self.assertNotIn("new_song", tools)
        start = {b["function"]["name"] for b in by_phase["Start"]["tools"]}
        self.assertIn("song_shape", start)
        self.assertNotIn("add_drums", start)

    def test_wiki_tools_and_refused_tools(self):
        def extra(phase, messages):
            if phase != "sounds":
                return None
            done = sum(1 for m in messages if m["role"] == "assistant")
            script = [("wiki_search", {"words": "pickup"}), ("wiki_read", {"page": "Craft moves", "heading": "4"}),
                      ("save_song", {}), ("add_drums", {})]
            if done < len(script):
                name, args = script[done]
                return {"role": "assistant", "content": "", "tool_calls": [
                    {"id": "x%d" % done, "type": "function", "function": {"name": name, "arguments": json.dumps(args)}}]}
            return None
        self.model.extra = extra
        self.run_job()
        replies = {}
        for body in self.model.requests:
            for m in body["messages"]:
                if m["role"] == "tool":
                    replies.setdefault(m["name"], m["content"])
        self.assertIn("Loops and seams", replies["wiki_search"])
        self.assertIn("Do move 4", replies["wiki_read"])
        self.assertIn("isn't offered in this phase", replies["save_song"])
        self.assertIn("Error", replies["add_drums"])

    def test_gate_retries_then_fixed(self):
        self.ft.findings = [finding()]
        job = self.run_job()
        hook = job["phases"][3]
        self.assertEqual(len(hook["gates"]), 2)                     # failed, fixed by the model, passed
        self.assertFalse(hook["gates"][0][1]["ok"])
        self.assertTrue(all(g["ok"] for g in hook["gates"][1]))
        fix_requests = [b for b in self.model.requests if "found what to fix" in b["messages"][1]["content"]]
        self.assertTrue(fix_requests)
        self.assertIn("polish", fix_requests[0]["messages"][1]["content"])
        self.assertEqual(job["status"], "done")

    def test_gate_fails_then_goes_on_with_a_note(self):
        self.ft.findings = [finding()]
        self.ft.fix_clears = False
        self.config.update({"gates": {"auto_fix": False}})
        job = self.run_job()
        self.assertEqual(job["status"], "done")
        self.assertEqual(job["phases"][3]["status"], "done with notes")
        self.assertTrue(any("gates still failing" in n for n in job["notes"]))

    def test_auto_fix(self):
        self.ft.findings = [finding()]
        self.model.fix_calls = []                  # the model fixes nothing
        job = self.run_job()
        hook = job["phases"][3]
        self.assertTrue(all(g["ok"] for g in hook["gates"][-1]))
        self.assertIn(("polish", {"touches": ["clashes"], "from_bar": 6, "to_bar": 6}), self.ft.calls)
        self.assertTrue(any("fixes itself" in n for n in job["notes"]))

    def test_review_gate(self):
        self.ft.score = 6
        job = self.run_job()
        dev = job["phases"][5]
        review = [g for g in dev["gates"][-1] if g["name"] == "review"][0]
        self.assertFalse(review["ok"])
        self.assertEqual(len(dev["gates"]), 2)
        self.assertEqual(job["status"], "done")

    def test_unsaved_song_saved_first(self):
        self.ft.dirty = True
        job = self.run_job()
        self.assertEqual(job["status"], "done")
        self.assertLess(self.ft.names().index("save_song"), self.ft.names().index("finish_song"))
        self.assertTrue(any("changes" in n for n in job["notes"]))

    def test_name_taken(self):
        self.ft.disk["Songsmith test Meadow"] = object()
        job = self.run_job()
        self.assertEqual(job["song"]["name"], "Songsmith test Meadow 2")

    def test_versions_and_options(self):
        jobs = self.store.add(dict(FORM, versions=2, options={"drums": False, "full_tools": True}))
        self.assertEqual([j["title"] for j in jobs], ["Songsmith test Meadow (v1)", "Songsmith test Meadow (v2)"])
        self.runner.run_once()
        tools = {t["function"]["name"] for b in self.model.requests for t in b["tools"]}
        self.assertNotIn("add_drums", tools)
        self.assertIn("add_counter_melody", tools)      # the full set (/mcp)
        system = self.model.requests[0]["messages"][0]["content"]
        self.assertIn("version 1 of 2", system)
        self.assertIn("Drums: none", system)

    def test_busy_and_down(self):
        self.ft.busy = 2
        self.model.fail = 2
        job = self.run_job()
        self.assertEqual(job["status"], "done")
        waits = [e for e in self.store.events(job["id"]) if e["kind"] == "wait"]
        self.assertTrue(any("busy" in e["text"] for e in waits))
        self.assertTrue(any("HTTP 503" in e["text"] for e in waits))

    def test_down_for_good_pauses_the_queue(self):
        self.ft.stop()
        self.config.update({"limits": {"wait_minutes": 0.05}})
        job = self.run_job()
        self.assertEqual(job["status"], "waiting")
        self.assertIn("can't be reached", job["error"])
        self.assertTrue(self.store.paused())
        self.assertIn("can't be reached", self.store.message())
        self.store.set_paused(False)
        self.assertEqual(self.store.message(), "")

    def test_wiki_missing_pauses(self):
        self.config.update({"wiki_path": os.path.join(self.tmp, "nowhere")})
        job = self.run_job()
        self.assertEqual(job["status"], "waiting")
        self.assertTrue(self.store.paused())
        self.assertNotIn("new_song", self.ft.names())

    def test_pause_and_resume(self):
        """paused during phase 3, the job waits at phase 3; a new harness from disk resumes it from its backup"""
        def extra(phase, messages):
            if phase == "sounds" and not self.store.paused():
                self.store.set_paused(True)
            return None
        self.model.extra = extra
        jobs = self.store.add(dict(FORM))
        self.runner.run_once()
        job = self.store.get(jobs[0]["id"])
        self.assertEqual(job["status"], "waiting")
        self.assertEqual(job["phase"], 2)
        self.assertTrue(job["resume"])
        self.assertFalse(self.ft.dirty)
        self.model.extra = None
        self.reload()
        self.store.set_paused(False)
        self.ft.song.name = "Other"            # FamiTracker has another song open meanwhile
        self.ft.song.file = "Other.ftm"
        self.runner.run_once()
        job = self.store.get(jobs[0]["id"])
        self.assertEqual(job["status"], "done", job.get("error"))
        calls = self.ft.names()
        self.assertIn("open_song", calls)
        self.assertIn(("restore_backup", {"backup": "songsmith %s sounds" % job["id"]}), self.ft.calls)

    def test_restart_finds_running_job(self):
        jobs = self.store.add(dict(FORM))
        job = self.store.get(jobs[0]["id"])
        job["status"] = "running"
        job["phase"] = 4
        save_json(self.store.job_path(job["id"]), job)
        self.reload()
        job = self.store.get(job["id"])
        self.assertEqual(job["status"], "waiting")
        self.assertTrue(job["resume"])
        self.assertTrue(any("resumes there" in n for n in job["notes"]))

    def test_cancel_running(self):
        def extra(phase, messages):
            if phase == "hook":
                job = self.runner.current
                self.store.control(job["id"], "skip")
            return None
        self.model.extra = extra
        job = self.run_job()
        self.assertEqual(job["status"], "skipped")
        self.assertEqual(job["phase"], 3)
        self.assertFalse(self.ft.dirty)

    def test_time_limits(self):
        self.config.update({"limits": {"job_hours": 0}})
        job = self.run_job()
        self.assertEqual(job["status"], "done")
        skipped = [p["id"] for p in job["phases"] if p["status"] == "skipped"]
        self.assertEqual(skipped, [p["id"] for p in PHASES[:-1]])        # straight to the finish
        self.assertTrue(job["ogg_files"])

    def test_model_timeout(self):
        self.model.delay = 2.0
        self.config.update({"model": {"timeout_seconds": 0.5}, "limits": {"wait_minutes": 0.2}})
        job = self.run_job()
        self.assertEqual(job["status"], "waiting")
        self.assertIn("model", job["error"])
        self.assertTrue(self.store.paused())

    def test_queue_order(self):
        a = self.store.add(dict(FORM, name="Songsmith test A"))[0]
        b = self.store.add(dict(FORM, name="Songsmith test B"))[0]
        c = self.store.add(dict(FORM, name="Songsmith test C", priority=1))[0]
        self.assertEqual(self.store.next_job()["id"], c["id"])
        self.store.move(b["id"], -10)
        self.assertEqual([j["id"] for j in self.store.listing()][0], b["id"])
        self.store.control(b["id"], "skip")
        self.assertEqual(self.store.next_job()["id"], c["id"])
        self.store.set_paused(True)
        self.assertIsNone(self.store.next_job())
        self.reload()
        self.assertTrue(self.store.paused())
        self.assertEqual(len(self.store.listing()), 3)
        self.assertEqual(self.store.get(a["id"])["status"], "waiting")

    def test_background_thread(self):
        self.store.add(dict(FORM))
        self.runner.start()
        deadline = time.time() + 30
        while time.time() < deadline and self.store.listing()[0]["status"] != "done":
            time.sleep(0.1)
        self.assertEqual(self.store.listing()[0]["status"], "done")
        self.runner.stop()


if __name__ == "__main__":
    unittest.main()

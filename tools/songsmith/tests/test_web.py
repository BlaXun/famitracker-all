import json
import os
import unittest
import urllib.error
import urllib.request

from helpers import FORM, HarnessCase, slurp
from songsmith.web import App, private, serve


class WebTests(HarnessCase):
    def setUp(self):
        super().setUp()
        self.app = App(self.store, self.config, self.runner, self.data)
        self.server = serve(self.app, "127.0.0.1", 0)
        self.base = "http://127.0.0.1:%d" % self.server.server_address[1]

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        super().tearDown()

    def get(self, path, headers=None):
        with urllib.request.urlopen(urllib.request.Request(self.base + path, headers=headers or {})) as r:
            return r.status, dict(r.headers), r.read()

    def post(self, path, body=None, headers=None):
        request = urllib.request.Request(self.base + path, data=json.dumps(body or {}).encode(), method="POST",
                                         headers=dict({"Content-Type": "application/json"}, **(headers or {})))
        try:
            with urllib.request.urlopen(request) as r:
                return r.status, json.loads(r.read())
        except urllib.error.HTTPError as error:
            return error.code, json.loads(error.read() or b"{}")

    def test_page(self):
        status, headers, body = self.get("/")
        self.assertEqual(status, 200)
        self.assertIn(b"<title>Songsmith</title>", body)
        self.assertNotIn(b"http://", body.replace(b"http://www.w3.org", b""))      # nothing from elsewhere

    def test_add_list_and_control(self):
        status, reply = self.post("/api/jobs", dict(FORM, versions=2, priority=1))
        self.assertEqual(status, 200)
        self.assertEqual(len(reply["jobs"]), 2)
        a, b = reply["jobs"]
        state = json.loads(self.get("/api/state")[2])
        self.assertEqual([j["id"] for j in state["jobs"]], [a, b])
        self.assertEqual(len(state["kinds"]), 13)
        self.assertIn("VRC6 Plus", state["chips"])
        self.assertTrue(state["templates"])
        self.assertEqual(len(state["phases"]), 8)
        self.post("/api/jobs/%s/down" % a)
        state = json.loads(self.get("/api/state")[2])
        self.assertEqual([j["id"] for j in state["jobs"]], [b, a])
        self.post("/api/queue/pause")
        self.assertTrue(json.loads(self.get("/api/state")[2])["paused"])
        self.post("/api/queue/resume")
        self.assertEqual(self.post("/api/jobs/%s/skip" % a)[1]["status"], "skipped")
        self.assertEqual(self.post("/api/jobs/%s/retry" % a)[1]["status"], "waiting")
        self.assertEqual(self.post("/api/jobs/%s/remove" % b)[1], {"removed": b})
        self.assertEqual(len(json.loads(self.get("/api/state")[2])["jobs"]), 1)
        status, reply = self.post("/api/jobs", dict(FORM, kind="sleepy"))
        self.assertEqual(status, 400)
        self.assertIn("Unknown kind", reply["error"])
        status, _ = self.post("/api/jobs", {"name": ""})
        self.assertEqual(status, 400)
        status, _ = self.post("/api/jobs", FORM, {"Origin": "http://evil.example"})
        self.assertEqual(status, 403)

    def test_job_page_events_files_and_verdict(self):
        job_id = self.post("/api/jobs", FORM)[1]["jobs"][0]
        self.runner.run_once()
        job = json.loads(self.get("/api/jobs/" + job_id)[2])
        self.assertEqual(job["status"], "done")
        events = json.loads(self.get("/api/jobs/%s/events?after=0" % job_id)[2])["events"]
        kinds = {e["kind"] for e in events}
        self.assertTrue({"phase", "call", "result", "gate", "job"} <= kinds)
        later = json.loads(self.get("/api/jobs/%s/events?after=%d" % (job_id, events[-1]["n"]))[2])["events"]
        self.assertEqual(len(later), 1)
        name = job["ogg_files"][0]
        status, headers, data = self.get("/api/jobs/%s/files/%s" % (job_id, urllib.request.quote(name)))
        self.assertEqual(headers["Content-Type"], "audio/ogg")
        self.assertTrue(data.startswith(b"OggS"))
        status, headers, part = self.get("/api/jobs/%s/files/%s" % (job_id, urllib.request.quote(name)),
                                         {"Range": "bytes=0-3"})
        self.assertEqual((status, part), (206, b"OggS"))
        self.assertIn("text/plain", self.get("/api/jobs/%s/files/report.md" % job_id)[1]["Content-Type"])
        with self.assertRaises(urllib.error.HTTPError):
            self.get("/api/jobs/%s/files/..%%2Fconfig.json" % job_id)
        status, reply = self.post("/api/jobs/%s/verdict" % job_id, {"words": "Lovely, the bass is too loud"})
        self.assertEqual(status, 200, reply)
        self.assertIn("Verdicts", reply["filed"])
        self.assertIn('"Lovely, the bass is too loud"', slurp(os.path.join(self.wiki, "Verdicts.md")))
        self.assertIn('verdict: "Lovely, the bass is too loud"',
                      slurp(os.path.join(self.wiki, "Song logs", "Songsmith test Meadow.md")))
        self.assertEqual(self.post("/api/jobs/%s/verdict" % job_id, {"words": " "})[0], 400)

    def test_settings_and_test(self):
        settings = json.loads(self.get("/api/settings")[2])
        self.assertTrue(settings["famitracker"]["token_set"])
        self.assertNotIn("token", settings["famitracker"])
        self.post("/api/settings", {"famitracker": {"token": "", "url": self.ft.url}, "gates": {"review_score": 7}})
        self.assertEqual(self.config.get()["famitracker"]["token"], "secret")      # empty keeps the saved code
        self.assertEqual(self.config.get()["gates"]["review_score"], 7)
        self.assertTrue(os.path.exists(self.config.path))
        status, result = self.post("/api/test", {})
        self.assertTrue(result["model"]["ok"], result)
        self.assertTrue(result["famitracker"]["ok"], result)
        self.assertIn("tools", result["famitracker"]["text"])
        self.assertIn("pages", result["wiki"]["text"])
        self.assertIn("calm", result["wiki"]["missing_kinds"])        # the test wiki has two kinds
        self.assertTrue(any("Kind pages missing" in w for w in result["wiki"]["warnings"]))
        status, result = self.post("/api/test", {"famitracker": {"token": "wrong"}})
        self.assertFalse(result["famitracker"]["ok"])

    def test_templates(self):
        status, reply = self.post("/api/templates", dict(FORM, label="Mine"))
        self.assertEqual(reply["templates"][-1]["label"], "Mine")

    def test_private_addresses(self):
        for ok in ("127.0.0.1", "192.168.1.5", "10.0.0.2", "100.101.1.2", "::1", "::ffff:192.168.0.3", "fe80::1"):
            self.assertTrue(private(ok), ok)
        for bad in ("8.8.8.8", "100.128.0.1", "2001:4860::1", "nonsense"):
            self.assertFalse(private(bad), bad)


if __name__ == "__main__":
    unittest.main()

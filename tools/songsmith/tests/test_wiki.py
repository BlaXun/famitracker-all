import os
import shutil
import tempfile
import unittest

from helpers import slurp
from songsmith.phases import PHASES, page_list
from songsmith.wiki import Wiki

HERE = os.path.dirname(os.path.abspath(__file__))


class WikiTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.path = os.path.join(self.tmp, "wiki")
        shutil.copytree(os.path.join(HERE, "wiki"), self.path)
        self.wiki = Wiki(self.path)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_pages_and_sections(self):
        self.assertEqual(self.wiki.find("kind - LIVELY.md"), "Kind - lively")
        self.assertTrue(self.wiki.text("Kind - lively").startswith("# Kind: lively"))     # no front matter
        self.assertIn("Do move 7", self.wiki.section("Craft moves", "7"))
        self.assertNotIn("move 8", self.wiki.section("Craft moves", "7"))
        self.assertIn("Do move 11", self.wiki.section("Craft moves", "11"))
        self.assertIn("F1:", self.wiki.cookbook("lively")[1])
        self.assertNotIn("F9", self.wiki.cookbook("lively")[1])
        self.assertEqual(self.wiki.cookbook("cozy")[0], "Hook cookbook - walking")
        self.assertEqual(self.wiki.examples("lively"), ["Example - Test Song", "Example - Second Song"])
        self.assertEqual(self.wiki.example_for("lively", 2), "Example - Second Song")
        self.assertIn("| cold |", self.wiki.flavour_rows("cold"))
        self.assertNotIn("| hot |", self.wiki.flavour_rows("cold"))
        verdicts = self.wiki.verdicts_for("cozy")
        self.assertIn("Cozy Hut", verdicts)

    def test_tools_bounded(self):
        found = self.wiki.search("move pickup intro")
        self.assertIn("Loops and seams", found)
        self.assertNotIn("raw/", found)      # raw pages are never offered
        self.assertIn("No page", self.wiki.read("Nothing here"))
        self.assertIn("No section", self.wiki.read("Craft moves", "99"))
        self.assertLessEqual(len(self.wiki.read("Craft moves", limit=200)), 1200)
        self.assertIn("Do move 3", self.wiki.read("Craft moves", "3"))

    def test_check(self):
        check = self.wiki.check(["lively", "cozy", "calm"])
        self.assertEqual(check["missing_kinds"], ["calm"])
        os.remove(os.path.join(self.path, "AGENTS.md"))
        self.wiki.reload()
        self.assertTrue(any("AGENTS.md" in w for w in self.wiki.check(["lively"])["warnings"]))

    def test_phase_pages(self):
        job = {"kind": "lively", "flavour": "cold", "version": 1, "options": {}}
        titles = [t for t, _ in page_list(PHASES[0], job, self.wiki)]
        self.assertEqual(titles[:4], ["index", "How to compose a song here", "Kind - lively", "Example - Test Song"])
        self.assertIn("Craft moves", titles)
        self.assertTrue(any(t.startswith("Flavours") for t in titles))
        self.assertTrue(any(t.startswith("Verdicts") for t in titles))
        hook = [t for t, _ in page_list(PHASES[3], job, self.wiki)]
        self.assertTrue(hook[0].startswith("Hook cookbook - fast, section lively"))
        finish = [t for t, _ in page_list(PHASES[7], job, self.wiki)]
        self.assertIn("Loops and seams", finish)

    def test_filing(self):
        page = self.wiki.file_song_log("Songsmith test Meadow", "---\nverdict: none yet\n---\n# Song log\n", "lively")
        self.assertEqual(page, "Song logs/Songsmith test Meadow")
        self.assertIn("[[Song logs/Songsmith test Meadow]] (lively, guided)", slurp(os.path.join(self.path, "log.md")))
        again = self.wiki.file_song_log("Songsmith test Meadow", "x", "lively")
        self.assertEqual(again, "Song logs/Songsmith test Meadow (2)")     # never over a page
        changed = self.wiki.file_verdict("Songsmith test Meadow", "nice tune, drums too loud", page)
        self.assertEqual(changed, ["Verdicts verbatim", "Verdicts", "song log", "log"])
        verbatim = slurp(os.path.join(self.path, "raw/verdicts/Verdicts verbatim.md"))
        self.assertTrue(verbatim.rstrip().endswith('- On "Songsmith test Meadow": "nice tune, drums too loud"'))
        lines = slurp(os.path.join(self.path, "Verdicts.md")).split("\n")
        sep = [i for i, l in enumerate(lines) if l.startswith("|---")][0]
        self.assertIn("Songsmith test Meadow", lines[sep + 1])
        self.assertIn("to be compiled | [[Song logs/Songsmith test Meadow]]", lines[sep + 1])
        log = slurp(os.path.join(self.path, "Song logs/Songsmith test Meadow.md"))
        self.assertIn('verdict: "nice tune, drums too loud"', log)
        self.assertIn("## Verdict", log)
        # nothing else changed
        self.assertEqual(slurp(os.path.join(self.path, "Craft moves.md")),
                         slurp(os.path.join(HERE, "wiki", "Craft moves.md")))

    def test_crlf_kept(self):
        path = os.path.join(self.path, "Verdicts.md")
        text = slurp(path).replace("\n", "\r\n")
        with open(path, "w", newline="") as f:
            f.write(text)
        self.wiki.file_verdict("X", "ok")
        self.assertNotIn("\n", slurp(path, "rb").decode().replace("\r\n", ""))


if __name__ == "__main__":
    unittest.main()

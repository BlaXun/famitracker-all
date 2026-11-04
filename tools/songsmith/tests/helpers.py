import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from songsmith.config import Config          # noqa: E402
from songsmith.runner import Runner          # noqa: E402
from songsmith.store import Store            # noqa: E402

from fakes import FakeFamiTracker, ScriptedModel   # noqa: E402

FORM = {"name": "Songsmith test Meadow", "about": "A bright meadow stage.", "kind": "lively", "flavour": "",
        "chip": "VRC6", "length": "short"}


def slurp(path, mode="r"):
    with open(path, mode, **({} if "b" in mode else {"encoding": "utf-8"})) as f:
        return f.read()


class HarnessCase(unittest.TestCase):
    """a data folder, a copy of the test wiki, a fake FamiTracker and a scripted model"""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="songsmith-test-")
        self.wiki = os.path.join(self.tmp, "wiki")
        shutil.copytree(os.path.join(HERE, "wiki"), self.wiki)
        self.ft = FakeFamiTracker().start()
        self.model = ScriptedModel().start()
        self.data = os.path.join(self.tmp, "data")
        self.config = Config(os.path.join(self.data, "config.json"), self.overrides())
        self.store = Store(self.data)
        self.runner = Runner(self.store, self.config, sleep=lambda s: None)

    def overrides(self):
        return {"model": {"base_url": self.model.url, "name": "fake-model", "timeout_seconds": 20},
                "famitracker": {"url": self.ft.url + "/mcp/core", "token": "secret", "timeout_seconds": 20},
                "wiki_path": self.wiki,
                "limits": {"wait_minutes": 0.5, "retries": 1, "max_turns_per_phase": 12},
                "gates": {"yardstick_max_misses": 20, "review_score": 8}}

    def tearDown(self):
        self.runner.quit.set()
        self.ft.stop()
        self.model.stop()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def reload(self):
        self.store = Store(self.data)
        self.runner = Runner(self.store, self.config, sleep=lambda s: None)

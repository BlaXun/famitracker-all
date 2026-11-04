#!/usr/bin/env python3
"""Songsmith: queue NES songs for a local model to compose in FamiTracker, overnight.

  songsmith.py serve [--data DIR] [--host H] [--port N]   the web page and the worker (the systemd service runs this)
  songsmith.py test  [--data DIR]                          test the model, FamiTracker and the wiki
  songsmith.py run   [--data DIR]                          work through the queue once, without the page

The settings are DIR/config.json (default DIR: ~/.local/share/songsmith); the page's Settings change them too.
"""

import argparse
import json
import os
import signal
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from songsmith.config import Config          # noqa: E402
from songsmith.runner import Runner          # noqa: E402
from songsmith.store import Store            # noqa: E402
from songsmith.web import App, serve         # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["serve", "test", "run"], nargs="?", default="serve")
    parser.add_argument("--data", default=os.environ.get("SONGSMITH_DATA",
                                                         os.path.expanduser("~/.local/share/songsmith")))
    parser.add_argument("--host")
    parser.add_argument("--port", type=int)
    args = parser.parse_args()
    os.makedirs(args.data, exist_ok=True)
    config = Config(os.path.join(args.data, "config.json"))
    if not os.path.exists(config.path):
        config.update({})       # write the defaults once, to edit by hand
    store = Store(args.data)
    runner = Runner(store, config)
    app = App(store, config, runner, args.data)
    if args.command == "test":
        print(json.dumps(app.test(), indent=1, ensure_ascii=False))
        return
    if args.command == "run":
        while runner.run_once():
            pass
        return
    web = config.get()["web"]
    host, port = args.host or web["host"], args.port or web["port"]
    server = serve(app, host, port)
    runner.start()
    print("Songsmith on http://%s:%d/ (data in %s)" % (host, port, args.data), flush=True)
    done = threading.Event()

    def stop(*_):
        done.set()
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    done.wait()
    print("Stopping: the running song is saved and resumes at its phase next time.", flush=True)
    runner.stop(wait=120)
    server.shutdown()


if __name__ == "__main__":
    main()

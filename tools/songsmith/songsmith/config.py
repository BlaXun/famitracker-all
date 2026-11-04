"""Settings: defaults, loading and saving (a JSON file in the data folder)."""

import copy
import json
import os
import threading

KINDS = ["calm", "cozy", "lively", "action", "mysterious", "adventure", "floating", "playful", "festive",
         "bittersweet", "tense", "melancholic", "atmospheric"]

FLAVOURS = ["cold", "hot", "wet", "mechanical", "nature", "spooky", "airy", "ancient", "magical"]

# The chips a job can ask for, as new_song's expansion takes them
CHIPS = {
    "VRC6": ["VRC6"],
    "VRC6 Plus": ["VRC6 Plus"],
    "2A03 only": [],
    "VRC7": ["VRC7"],
    "FDS": ["FDS"],
    "MMC5": ["MMC5"],
    "N163": ["N163"],
    "5B": ["5B"],
}

LENGTHS = {"short": 28, "normal": 40, "long": 56}

DEFAULTS = {
    "model": {
        "base_url": "http://127.0.0.1:8080/v1",
        "name": "qwen3.8-flash-next",
        "api_key": "",
        "temperature": 0.6,
        "max_tokens": 4096,
        "timeout_seconds": 900,
        "extra": {},
    },
    "famitracker": {
        "url": "http://127.0.0.1:46600",
        "token": "",
        "timeout_seconds": 900,
    },
    "wiki_path": "/home/blaxun/Documents/Hermes-Memory/FamiTracker Wiki",
    "file_song_logs": True,
    "limits": {
        "max_turns_per_phase": 40,
        "phase_minutes": 60,
        "job_hours": 8,
        "retries": 2,
        "wait_minutes": 120,
        "tool_result_chars": 6000,
        "context_chars": 60000,
    },
    "gates": {
        "review_score": 8,
        "yardstick_max_misses": 3,
        "auto_fix": True,
        "mix_max_over_db": 3.0,
        "loudness_low": -32.0,
        "loudness_high": -10.0,
    },
    "unsaved_song": "save",
    "web": {"host": "0.0.0.0", "port": 8765},
}


def merged(base, over):
    """base with over's keys laid over it, nested dicts merged"""
    out = copy.deepcopy(base)
    for key, value in (over or {}).items():
        if isinstance(value, dict) and isinstance(out.get(key), dict):
            out[key] = merged(out[key], value)
        else:
            out[key] = value
    return out


class Config:
    """The settings, kept in data/config.json; get() gives a copy"""

    def __init__(self, path, overrides=None):
        self.path = path
        self.lock = threading.Lock()
        data = {}
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
        self.data = merged(DEFAULTS, merged(data, overrides or {}))

    def get(self):
        with self.lock:
            return copy.deepcopy(self.data)

    def update(self, changes):
        with self.lock:
            self.data = merged(self.data, changes)
            save_json(self.path, self.data)
            return copy.deepcopy(self.data)

    def public(self):
        """the settings for the web page: the secrets shown only as set or not"""
        data = self.get()
        data["famitracker"]["token_set"] = bool(data["famitracker"].pop("token", ""))
        data["model"]["api_key_set"] = bool(data["model"].pop("api_key", ""))
        return data


def save_json(path, data):
    """write a JSON file whole or not at all"""
    folder = os.path.dirname(path)
    if folder:
        os.makedirs(folder, exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, ensure_ascii=False)
    os.replace(tmp, path)


def load_json(path, default=None):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return default

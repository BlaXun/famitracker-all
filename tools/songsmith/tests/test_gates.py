import unittest

from helpers import HarnessCase
from fakes import finding
from songsmith import gates, yardstick
from songsmith.mcp import McpClient, ToolReply, parse_reply
from songsmith.net import Patience
from songsmith.song import parse_parts, parse_song, parse_steps, pitch, review_score
from songsmith.wiki import Wiki

GET_SONG = '''"Frost", file Frost.ftm
Tempo: 69.2 BPM (speed 13, tempo 150, NTSC 60 Hz); 4 rows a beat, 4 beats a bar (16 rows)
Length: 48 bars (12 frames of 64 rows)
Sections: "Intro" bars 1-4, "A" bars 5-12, "A'" bars 13-20
Channels (expansion: VRC6):
  1 2A03 Pulse 1: 276 notes in bars 1-48, D-2 to A#3, instruments "Pluck" (01), in the middle
  2 2A03 Pulse 2: 32 notes in bars 21-28, A#2 to D-3, instruments "Pluck" (01), in the middle
  4 2A03 Noise: 147 hits in bars 1-48, 5-# to D-#, in the middle
Key: D major (chosen for this track: the tools use it when not given a key); chords that fit
Instruments: 00 "Calm lead" (VRC6), 01 "Pluck" (2A03), 02 "Bass" (2A03)'''


class ParseTests(unittest.TestCase):
    def test_song(self):
        s = parse_song(GET_SONG)
        self.assertEqual((s["title"], s["file"], s["bars"], s["instruments"]), ("Frost", "Frost.ftm", 48, 3))
        self.assertEqual(s["sections"][1], ("A", 5, 12))
        self.assertEqual(s["sections"][2], ("A'", 13, 20))
        self.assertEqual([c["name"] for c in s["channels"]], ["2A03 Pulse 1", "2A03 Pulse 2", "2A03 Noise"])
        self.assertEqual(s["channels"][2]["notes"], 147)
        self.assertTrue(s["key_chosen"])
        self.assertEqual(s["key"], "D major")

    def test_steps_and_pitches(self):
        bars, changes = parse_steps('VRC6 Pulse 1, bars 4-6:\nInstruments: "Calm lead" (00), from bar 5 beat 1 '
                                    '"Bell" (08).\n4: . . . . C#4v9 - - -\n5: A3v9 - - F#3 A3 - B3 -\n6: . . . . . . . .')
        self.assertEqual(bars[4][4], "C#4v9")
        self.assertEqual(changes, [(1, "Calm lead"), (5, "Bell")])
        self.assertEqual(pitch("A3"), 69)
        self.assertEqual(pitch("C#4v9"), 73)
        self.assertEqual(pitch("Bb2"), 58)
        self.assertIsNone(pitch("5-#"))
        self.assertIsNone(pitch("-"))

    def test_review(self):
        text = ("Review (calm), bars 1-48 in D major: 9/10.\nParts: melody on VRC6 Pulse 1; chords on 2A03 Pulse 2; "
                "bass on 2A03 Triangle (its roots doubled on VRC6 Sawtooth); drums on 2A03 Noise.")
        self.assertEqual(review_score(text), 9)
        self.assertEqual(parse_parts(text), {"melody": "VRC6 Pulse 1", "bass": "2A03 Triangle", "chords": "2A03 Pulse 2"})

    def test_reply_files_and_streams(self):
        reply = ToolReply("Saved Town.ogg next to the song: http://192.168.1.10:46600/files/Town%20Night.ogg?code=1",
                          False, [{"type": "resource_link", "uri": "famitracker://files/Town%202.ogg"}])
        self.assertEqual(reply.files(), ["Town 2.ogg", "Town Night.ogg"])
        self.assertEqual(parse_reply('event: message\ndata: {"jsonrpc":"2.0","id":1,"result":{}}\n'),
                         {"jsonrpc": "2.0", "id": 1, "result": {}})


class GateTests(HarnessCase):
    def client(self):
        return McpClient(self.ft.url, "secret", core=True, timeout=10, patience=Patience(0.1))

    def test_check_song_gate(self):
        mcp = self.client()
        self.assertTrue(gates.check_song(mcp, "lively", self.config.get())["ok"])
        self.ft.findings = [finding(), finding("clash", "low"), dict(finding("held", "medium"), fix=None)]
        g = gates.check_song(mcp, "lively", self.config.get())
        self.assertFalse(g["ok"])
        self.assertEqual(g["data"]["worth"], 1)               # a low one and one without a fix don't count
        self.assertIn('polish {"touches": ["clashes"], "from_bar": 6, "to_bar": 6}', g["detail"])

    def test_review_and_mix(self):
        mcp = self.client()
        settings = self.config.get()
        self.assertTrue(gates.review(mcp, "lively", settings)["ok"])
        self.ft.score = 7.5
        self.assertFalse(gates.review(mcp, "lively", settings)["ok"])
        mix = gates.measure_mix(mcp, "lively", settings)
        self.assertTrue(mix["ok"], mix["summary"])
        self.assertEqual(mix["data"]["lufs"], -20.1)

    def test_yardstick_counts(self):
        mcp = self.client()
        for tool in ("song_shape", "grow_melody", "add_chords_and_bass", "add_drums", "add_echo"):
            self.ft.tool(tool, {})
        for n in range(12):
            self.ft.tool("design_instrument", {"name": "S%d" % n})
        rows, song = yardstick.measure(mcp, "lively", "VRC6 Pulse 1", "2A03 Triangle", wiki=Wiki(self.wiki))
        keys = {r["key"]: r for r in rows}
        self.assertEqual(set(keys), {"parts", "instruments", "rhythms", "repeats", "hook", "register", "bass",
                                     "changes", "ending"})
        self.assertEqual(keys["register"]["value"], "E4")
        self.assertTrue(keys["instruments"]["ok"])
        self.assertEqual(keys["rhythms"]["value"], round(8 * 4 / 24, 2))
        self.assertFalse(keys["changes"]["ok"])           # the fake's parts never change
        self.assertTrue(keys["changes"]["advice"])
        text = yardstick.rows_text(rows)
        self.assertIn("UNDER", text)
        # the wiki's own "When under" words are the advice where it has the row
        self.assertEqual(keys["parts"]["advice"] if not keys["parts"]["ok"] else "add a part that has a job",
                         "add a part that has a job")


if __name__ == "__main__":
    unittest.main()

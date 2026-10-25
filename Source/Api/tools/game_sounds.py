# FamiTracker - NES/Famicom sound tracker
# Copyright (C) 2026  The beginner-friendly FamiTracker contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.  To obtain a
# copy of the GNU Library General Public License, write to the Free
# Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

# Writes Source/Api/GameSoundsData.h, the sounds FamiTracker carries from the
# studies of the soundtracks that may be public (GameSounds.h), from a folder
# of studies (the reference library's JSON files, as study_nsf writes them):
#
#   python3 Source/Api/tools/game_sounds.py <folder of studies> Source/Api/GameSoundsData.h
#
# Only the games in PUBLIC are read, only their sounds are kept (each sound's
# channel, part, chip, words, sequences and waves, and the title and number of
# the tracks it plays in, which name it), and a sound that several tracks of a
# game play is kept once, with the tracks it plays in. The values are read as
# ReadReferenceFile (References.cpp) reads them, so a built-in sound and the
# same sound in the library compare equal (GameSoundKey).

import json
import os
import re
import sys

# The soundtracks the user okayed as public (25 October 2026), by the slugs of
# their studies' ids ("castlevania-3-26": the game castlevania-3, track 26)
PUBLIC = [
	"castlevania", "castlevania-3", "dr-mario", "final-fantasy-3", "journey-to-silius", "nintendo-world-cup",
	"partena-no-kagami", "punch-out", "robot-block", "strider-hiryu", "super-c-super-contra", "the-legend-of-zelda",
]

SEQUENCES = [("volume", "V"), ("arpeggio", "A"), ("pitch", "P"), ("hi_pitch", "H"), ("duty", "D")]
MAX_ITEMS = 252			# References.cpp's
FDS_WAVE_SIZE, FDS_MOD_SIZE = 64, 32


def number(value):
	return isinstance(value, (int, float)) and not isinstance(value, bool)


def clamp(value, low, high):
	return max(low, min(high, value))


def integer(value, default):
	return int(value) if number(value) else default


# A sequence as ReadSequence reads it: its values (at most MAX_ITEMS, -128-127), loop, release and whether it's fixed
def read_sequence(j):
	values = j.get("values") if isinstance(j, dict) else None
	items = []
	if isinstance(values, list):
		for v in values[:MAX_ITEMS]:
			if number(v):
				items.append(clamp(int(v), -128, 127))
	loop = integer(j.get("loop"), -1)
	release = integer(j.get("release"), -1)
	if loop >= len(items):
		loop = -1
	if release >= len(items):
		release = -1
	return {"items": items, "loop": loop, "release": release, "fixed": j.get("fixed") is True}


def sequence_text(letter, seq):
	text = letter + ",".join(str(v) for v in seq["items"])
	if seq["loop"] >= 0:
		text += "/L%d" % seq["loop"]
	if seq["release"] >= 0:
		text += "/R%d" % seq["release"]
	if seq["fixed"]:
		text += "/F"
	return text


# A sound as ReadReferenceFile reads it: the fields that make it (its data, as text) and those that say where it played
def read_sound(s):
	chip = s.get("chip") if isinstance(s.get("chip"), str) and s.get("chip") else "2A03"
	flags = ("n" if s.get("noise") is True else "") + ("t" if s.get("triangle") is True else "") + \
		("s" if s.get("sawtooth") is True else "")
	parts = []
	for name, letter in SEQUENCES:
		if isinstance(s.get(name), dict):
			seq = read_sequence(s[name])
			if seq["items"]:
				parts.append(sequence_text(letter, seq))
	wave, mod = s.get("fds_wave"), s.get("fds_modulation")
	if isinstance(wave, list) and len(wave) == FDS_WAVE_SIZE:
		parts.append("W" + ",".join(str(clamp(int(v), 0, 63) if number(v) else 32) for v in wave))
		if isinstance(mod, list) and len(mod) == FDS_MOD_SIZE:
			parts.append("M" + ",".join(str(clamp(int(v), 0, 7) if number(v) else 0) for v in mod))
		parts.append("S%d,%d,%d" % (clamp(integer(s.get("modulation_speed"), 0), 0, 4095),
			clamp(integer(s.get("modulation_depth"), 0), 0, 63), clamp(integer(s.get("modulation_delay"), 0), 0, 255)))
	waves = s.get("n163_waves")
	if isinstance(waves, list):
		read = []
		for w in waves[:17]:
			samples = []
			if isinstance(w, list):
				samples = [clamp(int(v), 0, 15) if number(v) else 8 for v in w[:33]]
			read.append(samples)
		whole = 1 <= len(read) <= 16 and all(4 <= len(w) <= 32 and len(w) == len(read[0]) for w in read)
		if whole:
			parts.append("N" + "|".join(",".join(str(v) for v in w) for w in read))
			steps = s.get("n163_wave_sequence")
			if isinstance(steps, dict) and len(read) > 1:
				seq = read_sequence(steps)
				seq["items"] = [clamp(v, 0, len(read) - 1) for v in seq["items"]]
				if seq["items"]:
					parts.append(sequence_text("Q", seq))
	words = s.get("words") if isinstance(s.get("words"), str) else ""
	return chip, flags, ";".join(parts), words


def text(value):
	return value if isinstance(value, str) else ""


def literal(value):
	out = '"'
	for c in value:
		if c == '\\' or c == '"':
			out += '\\' + c
		elif 32 <= ord(c) < 127:
			out += c
		else:
			out += "".join("\\x%02X" % b for b in c.encode("utf-8")) + '""'	# UTF-8 bytes, the next character apart
	return out + '"'


# A long literal in pieces (MSVC takes 16 KB a piece)
def long_literal(value, width=100):
	pieces = [value[i:i + width] for i in range(0, len(value), width)] or [""]
	return "\r\n\t\t".join(literal(p) for p in pieces)


def main():
	if len(sys.argv) != 3:
		sys.exit("game_sounds.py <folder of studies> <GameSoundsData.h>")
	folder, out_path = sys.argv[1], sys.argv[2]
	games = []			# (slug, name)
	tracks = []			# (game, track, title)
	sounds = []			# (game, chip, flags, data, words)
	uses = []			# (sound, track row, number, channel, role)
	known = {}			# (game, chip, flags, data) -> sound
	studies = {}
	for name in sorted(os.listdir(folder)):
		m = re.match(r"^(.+)-(\d+)\.json$", name)
		if not m or m.group(1) not in PUBLIC:
			continue
		with open(os.path.join(folder, name), encoding="utf-8") as f:
			d = json.load(f)
		if not isinstance(d, dict) or "reference" not in d:
			continue
		studies.setdefault(m.group(1), []).append((int(m.group(2)), name[:-5], d))
	for slug in PUBLIC:
		if slug not in studies:
			continue
		g = len(games)
		games.append((slug, text(studies[slug][0][2].get("game"))))
		for number_in_id, ident, d in sorted(studies[slug], key=lambda x: x[0]):
			t = len(tracks)
			tracks.append((g, integer(d.get("track"), number_in_id), text(d.get("title")), ident))
			for n, s in enumerate(d.get("sounds") or []):
				if not isinstance(s, dict):
					continue
				chip, flags, data, words = read_sound(s)
				key = (g, chip, flags, data)
				if key not in known:
					known[key] = len(sounds)
					sounds.append((g, chip, flags, data, words))
				uses.append((known[key], t, n + 1, text(s.get("channel")), text(s.get("role"))))
	lines = []
	lines.append("// The sounds of the studies of the soundtracks FamiTracker may carry (GameSounds.h), made by")
	lines.append("// Source/Api/tools/game_sounds.py from the reference library's studies: don't edit it, run that again.")
	lines.append("// %d games, %d tracks, %d sounds (%d as the tracks play them)." % (len(games), len(tracks), len(sounds), len(uses)))
	lines.append("")
	lines.append("const GameRow GAME_ROWS[] = {")
	for slug, name in games:
		lines.append("\t{ %s, %s }," % (literal(slug), literal(name)))
	lines.append("};")
	lines.append("")
	lines.append("const TrackRow TRACK_ROWS[] = {")
	for g, track, title, ident in tracks:
		lines.append("\t{ %d, %d, %s, %s }," % (g, track, literal(ident), literal(title)))
	lines.append("};")
	lines.append("")
	lines.append("const SoundRow SOUND_ROWS[] = {")
	for g, chip, flags, data, words in sounds:
		lines.append("\t{ %d, %s, %s, %s,\r\n\t\t%s }," % (g, literal(chip), literal(flags), literal(words), long_literal(data)))
	lines.append("};")
	lines.append("")
	lines.append("const UseRow USE_ROWS[] = {")
	for sound, t, n, channel, role in uses:
		lines.append("\t{ %d, %d, %d, %s, %s }," % (sound, t, n, literal(channel), literal(role)))
	lines.append("};")
	with open(out_path, "w", encoding="ascii", newline="") as f:
		f.write("\r\n".join(lines) + "\r\n")
	print("%s: %d games, %d tracks, %d sounds (%d uses)" % (out_path, len(games), len(tracks), len(sounds), len(uses)))


main()

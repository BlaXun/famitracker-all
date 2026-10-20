/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2014  Jonathan Liss
** Copyright (C) 2026  The beginner-friendly FamiTracker contributors
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.  To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
*/

// wav2ftm: turn a recording (whistle, hum, finger taps) into a FamiTracker
// text module that FamiTracker 0.4.6 opens with File > Import Text.

#include "Transcribe.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

using namespace transcribe;

namespace {

void Usage()
{
	std::printf(
		"Usage: wav2ftm [options] <input.wav> <output.txt>\n"
		"\n"
		"Turns a recording into a FamiTracker text module (File > Import Text).\n"
		"A note lasts as long as the sound that made it.\n"
		"\n"
		"  --mode melody|drums   whistle/hum/sing a melody, or tap/beatbox a rhythm (default: melody)\n"
		"  --bpm N               tempo, 4 rows per beat, 32-255 (default: 150)\n"
		"  --expansion vrc6|none sound chip (default: vrc6)\n"
		"  --channel N           melody channel: 0-2 = 2A03 pulse 1/pulse 2/triangle,\n"
		"                        5-7 = VRC6 pulse 1/pulse 2/sawtooth (default: 5 with VRC6, else 0)\n"
		"  --key K --scale S     snap notes to a scale, e.g. --key C --scale pentatonic\n"
		"                        (scales: chromatic, major, minor, pentatonic, minor-pentatonic)\n"
		"  --transpose N         shift by N semitones instead of the automatic octave choice\n"
		"  --pattern-length N    rows per pattern, 16-256 (default: 64)\n"
		"  --keep-silence        keep leading silence instead of starting at row 0\n"
		"  --title TEXT          song title (default: input file name)\n"
		"  --print               list what was detected\n");
}

bool ParseInt(const char *s, int &out)
{
	char *end = nullptr;
	const long v = std::strtol(s, &end, 10);
	if (end == s || *end != '\0')
		return false;
	out = int(v);
	return true;
}

std::string BaseName(const std::string &path)
{
	const size_t slash = path.find_last_of("/\\");
	std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
	const size_t dot = name.find_last_of('.');
	return dot == std::string::npos ? name : name.substr(0, dot);
}

const char *DrumName(Drum d)
{
	return d == Drum::Kick ? "kick" : (d == Drum::Snare ? "snare" : "hi-hat");
}

}	// namespace

int main(int argc, char **argv)
{
	std::string mode = "melody", input, output, title, keyName, scaleName;
	int bpm = 150, channel = -1, transpose = 0, patternLength = 64;
	bool haveTranspose = false, keepSilence = false, print = false;
	Expansion expansion = Expansion::VRC6;

	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		auto next = [&](const char *name) -> const char * {
			if (i + 1 >= argc) {
				std::fprintf(stderr, "wav2ftm: %s needs a value\n", name);
				std::exit(2);
			}
			return argv[++i];
		};
		if (a == "--help" || a == "-h") { Usage(); return 0; }
		else if (a == "--mode") mode = next("--mode");
		else if (a == "--bpm") { if (!ParseInt(next("--bpm"), bpm)) { std::fprintf(stderr, "wav2ftm: --bpm needs a number\n"); return 2; } }
		else if (a == "--expansion") {
			const std::string e = next("--expansion");
			if (e == "vrc6") expansion = Expansion::VRC6;
			else if (e == "none") expansion = Expansion::None;
			else { std::fprintf(stderr, "wav2ftm: unknown expansion '%s'\n", e.c_str()); return 2; }
		}
		else if (a == "--channel") { if (!ParseInt(next("--channel"), channel)) { std::fprintf(stderr, "wav2ftm: --channel needs a number\n"); return 2; } }
		else if (a == "--key") keyName = next("--key");
		else if (a == "--scale") scaleName = next("--scale");
		else if (a == "--transpose") { if (!ParseInt(next("--transpose"), transpose)) { std::fprintf(stderr, "wav2ftm: --transpose needs a number\n"); return 2; } haveTranspose = true; }
		else if (a == "--pattern-length") { if (!ParseInt(next("--pattern-length"), patternLength)) { std::fprintf(stderr, "wav2ftm: --pattern-length needs a number\n"); return 2; } }
		else if (a == "--keep-silence") keepSilence = true;
		else if (a == "--title") title = next("--title");
		else if (a == "--print") print = true;
		else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "wav2ftm: unknown option %s\n", a.c_str()); return 2; }
		else if (input.empty()) input = a;
		else if (output.empty()) output = a;
		else { Usage(); return 2; }
	}
	if (input.empty() || output.empty()) {
		Usage();
		return 2;
	}
	if (mode != "melody" && mode != "drums") {
		std::fprintf(stderr, "wav2ftm: --mode must be melody or drums\n");
		return 2;
	}
	if (bpm < 32 || bpm > 255) {
		std::fprintf(stderr, "wav2ftm: --bpm must be between 32 and 255\n");
		return 2;
	}
	if (patternLength < 16 || patternLength > 256) {
		std::fprintf(stderr, "wav2ftm: --pattern-length must be between 16 and 256\n");
		return 2;
	}
	if (channel < 0)
		channel = (expansion == Expansion::VRC6) ? 5 : 0;
	const bool validChannel = channel == 0 || channel == 1 || channel == 2 || (expansion == Expansion::VRC6 && channel >= 5 && channel <= 7);
	if (mode == "melody" && !validChannel) {
		std::fprintf(stderr, "wav2ftm: --channel %d cannot play a melody with this expansion\n", channel);
		return 2;
	}
	Scale scale = Scale::Chromatic;
	int root = 0;
	if (!scaleName.empty() && !ParseScale(scaleName, scale)) {
		std::fprintf(stderr, "wav2ftm: unknown scale '%s'\n", scaleName.c_str());
		return 2;
	}
	if (!keyName.empty() && !ParseKey(keyName, root)) {
		std::fprintf(stderr, "wav2ftm: unknown key '%s'\n", keyName.c_str());
		return 2;
	}

	Audio audio;
	std::string error;
	if (!ReadWav(input, audio, &error)) {
		std::fprintf(stderr, "wav2ftm: %s\n", error.c_str());
		return 1;
	}

	TextModuleOptions options;
	options.expansion = expansion;
	options.melodyChannel = channel;
	options.patternLength = patternLength;
	options.grid.tempo = bpm;	// speed 6 at 4 rows per beat: BPM = tempo * 6 / speed
	options.grid.speed = 6;
	options.title = title.empty() ? BaseName(input) : title;
	options.comment = "Transcribed from " + BaseName(input) + " by wav2ftm (" + mode + ", " + std::to_string(bpm) + " BPM)";

	std::vector<GridNote> melody;
	std::vector<GridHit> drums;

	if (mode == "melody") {
		const std::vector<PitchFrame> frames = TrackPitch(audio, PitchOptions());
		std::vector<Note> notes = SegmentNotes(frames, NoteOptions());
		if (notes.empty()) {
			std::fprintf(stderr, "wav2ftm: no notes found (is the recording loud enough?)\n");
			return 1;
		}
		const bool low = (channel == 2 || channel == 7);	// triangle and sawtooth suit bass lines
		const int shift = haveTranspose ? transpose : OctaveShiftFor(notes, low ? 55 : 67);
		Transpose(notes, shift);
		if (scale != Scale::Chromatic)
			SnapToScale(notes, root, scale);
		melody = Quantize(notes, options.grid, !keepSilence);
		if (print) {
			std::printf("%zu notes (shifted %+d semitones):\n", notes.size(), shift);
			for (size_t i = 0; i < melody.size(); ++i)
				std::printf("  row %4d  %s  %3d rows\n", melody[i].row, FamiTrackerNoteName(melody[i].midi).c_str(), melody[i].length);
		}
	}
	else {
		std::vector<Hit> hits = DetectHits(audio, HitOptions());
		if (hits.empty()) {
			std::fprintf(stderr, "wav2ftm: no hits found (is the recording loud enough?)\n");
			return 1;
		}
		const int classes = ClassifyHits(hits);
		drums = QuantizeHits(hits, options.grid, !keepSilence);
		if (print) {
			std::printf("%zu hits, %d kind(s) of sound:\n", hits.size(), classes);
			for (size_t i = 0; i < drums.size(); ++i)
				std::printf("  row %4d  %-6s volume %X\n", drums[i].row, DrumName(drums[i].drum), drums[i].volume);
		}
	}

	const std::string text = WriteTextModule(melody, drums, options);
	if (!ValidateTextModule(text, &error)) {
		std::fprintf(stderr, "wav2ftm: internal error, generated module is invalid: %s\n", error.c_str());
		return 1;
	}
	std::ofstream file(output.c_str(), std::ios::binary);
	if (!file) {
		std::fprintf(stderr, "wav2ftm: cannot write %s\n", output.c_str());
		return 1;
	}
	file << text;
	std::printf("Wrote %s. In FamiTracker: File > Import Text.\n", output.c_str());
	return 0;
}

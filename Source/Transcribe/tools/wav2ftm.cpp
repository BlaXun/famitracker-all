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
		"       wav2ftm [options] [--drums taps.wav] [--bass hum.wav] [melody.wav] <output.txt>\n"
		"\n"
		"Turns recordings into a FamiTracker text module (File > Import Text).\n"
		"A note lasts as long as the sound that made it. Give several recordings to\n"
		"get them all in one song: a melody, a drum part and a bass line.\n"
		"\n"
		"  --mode melody|drums   whistle/hum/sing a melody, or tap/beatbox a rhythm (default: melody)\n"
		"  --drums FILE          add a drum part from a recording of taps or beatboxing\n"
		"  --bass FILE           add a bass line from a recording (hum it), on the VRC6 sawtooth,\n"
		"                        or on the triangle with --expansion none\n"
		"  --bpm N               tempo, 4 rows per beat, 32-255 (default: 150)\n"
		"  --expansion vrc6|none sound chip (default: vrc6)\n"
		"  --channel N           melody channel: 0-2 = 2A03 pulse 1/pulse 2/triangle,\n"
		"                        5-7 = VRC6 pulse 1/pulse 2/sawtooth (default: 5 with VRC6, else 0)\n"
		"  --key K --scale S     snap notes to a scale, e.g. --key C --scale pentatonic\n"
		"                        (scales: chromatic, major, minor, pentatonic, minor-pentatonic)\n"
		"  --transpose N         shift the melody by N semitones instead of the automatic octave choice\n"
		"  --min-rest MS         breaks shorter than this many milliseconds are held through,\n"
		"                        so the note lasts until the next one (default: 150; 0 keeps\n"
		"                        every break as a rest)\n"
		"  --pattern-length N    rows per pattern, 16-256 (default: 64)\n"
		"  --keep-silence        keep leading silence instead of starting each recording at row 0;\n"
		"                        use it when the recordings all start at the same moment\n"
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

struct Settings {
	Grid grid;
	QuantizeOptions quantize;
	Scale scale = Scale::Chromatic;
	int root = 0;
	bool print = false;
};

bool Load(const std::string &path, Audio &audio)
{
	std::string error;
	if (!ReadWav(path, audio, &error)) {
		std::fprintf(stderr, "wav2ftm: %s\n", error.c_str());
		return false;
	}
	return true;
}

// Whistle/hum/sing -> rows. targetMidi picks the octave unless transpose is given.
bool TranscribeMelody(const std::string &path, const char *part, int targetMidi, const int *transpose,
	const Settings &settings, std::vector<GridNote> &rows)
{
	Audio audio;
	if (!Load(path, audio))
		return false;
	std::vector<Note> notes = SegmentNotes(TrackPitch(audio, PitchOptions()), NoteOptions());
	if (notes.empty()) {
		std::fprintf(stderr, "wav2ftm: no notes found in %s (is the recording loud enough?)\n", path.c_str());
		return false;
	}
	const int shift = transpose ? *transpose : OctaveShiftFor(notes, targetMidi);
	Transpose(notes, shift);
	if (settings.scale != Scale::Chromatic)
		SnapToScale(notes, settings.root, settings.scale);
	rows = Quantize(notes, settings.grid, settings.quantize);
	if (settings.print) {
		std::printf("%s: %zu notes (shifted %+d semitones):\n", part, rows.size(), shift);
		for (size_t i = 0; i < rows.size(); ++i) {
			const int end = rows[i].row + rows[i].length;
			const int rest = (i + 1 < rows.size() ? rows[i + 1].row : end) - end;
			std::printf("  row %4d  %s  %3d rows", rows[i].row, FamiTrackerNoteName(rows[i].midi).c_str(), rows[i].length);
			if (rest > 0)
				std::printf(", then %d rows rest", rest);
			std::printf("\n");
		}
	}
	return true;
}

// Taps/beatboxing -> drum hits on rows.
bool TranscribeDrums(const std::string &path, const Settings &settings, std::vector<GridHit> &rows)
{
	Audio audio;
	if (!Load(path, audio))
		return false;
	std::vector<Hit> hits = DetectHits(audio, HitOptions());
	if (hits.empty()) {
		std::fprintf(stderr, "wav2ftm: no hits found in %s (is the recording loud enough?)\n", path.c_str());
		return false;
	}
	const int classes = ClassifyHits(hits);
	rows = QuantizeHits(hits, settings.grid, settings.quantize.alignToStart);
	if (settings.print) {
		std::printf("drums: %zu hits, %d kind(s) of sound:\n", hits.size(), classes);
		for (size_t i = 0; i < rows.size(); ++i)
			std::printf("  row %4d  %-6s volume %X\n", rows[i].row, DrumName(rows[i].drum), rows[i].volume);
	}
	return true;
}

}	// namespace

int main(int argc, char **argv)
{
	std::string mode = "melody", title, keyName, scaleName, drumsFile, bassFile;
	std::vector<std::string> positional;
	int bpm = 150, channel = -1, transpose = 0, patternLength = 64, minRestMs = 150;
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
		else if (a == "--drums") drumsFile = next("--drums");
		else if (a == "--bass") bassFile = next("--bass");
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
		else if (a == "--min-rest") { if (!ParseInt(next("--min-rest"), minRestMs)) { std::fprintf(stderr, "wav2ftm: --min-rest needs a number of milliseconds\n"); return 2; } }
		else if (a == "--keep-silence") keepSilence = true;
		else if (a == "--title") title = next("--title");
		else if (a == "--print") print = true;
		else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "wav2ftm: unknown option %s\n", a.c_str()); return 2; }
		else positional.push_back(a);
	}

	// <input> <output>, or just <output> when --drums/--bass bring the recordings
	std::string input, output;
	if (positional.size() == 2) {
		input = positional[0];
		output = positional[1];
	}
	else if (positional.size() == 1 && (!drumsFile.empty() || !bassFile.empty())) {
		output = positional[0];
	}
	else {
		Usage();
		return 2;
	}
	if (mode != "melody" && mode != "drums") {
		std::fprintf(stderr, "wav2ftm: --mode must be melody or drums\n");
		return 2;
	}
	if (mode == "drums" && !input.empty() && !drumsFile.empty()) {
		std::fprintf(stderr, "wav2ftm: two drum recordings; give one with --drums\n");
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
	if (minRestMs < 0 || minRestMs > 5000) {
		std::fprintf(stderr, "wav2ftm: --min-rest must be between 0 and 5000 milliseconds\n");
		return 2;
	}
	const bool haveMelody = !input.empty() && mode == "melody";
	if (channel < 0)
		channel = (expansion == Expansion::VRC6) ? 5 : 0;
	const bool validChannel = channel == 0 || channel == 1 || channel == 2 || (expansion == Expansion::VRC6 && channel >= 5 && channel <= 7);
	if (haveMelody && !validChannel) {
		std::fprintf(stderr, "wav2ftm: --channel %d cannot play a melody with this expansion\n", channel);
		return 2;
	}
	const int bassChannel = (expansion == Expansion::VRC6) ? 7 : 2;	// sawtooth, or the triangle
	if (haveMelody && !bassFile.empty() && channel == bassChannel) {
		std::fprintf(stderr, "wav2ftm: the melody and the bass would share channel %d; pick another --channel\n", channel);
		return 2;
	}

	Settings settings;
	if (!scaleName.empty() && !ParseScale(scaleName, settings.scale)) {
		std::fprintf(stderr, "wav2ftm: unknown scale '%s'\n", scaleName.c_str());
		return 2;
	}
	if (!keyName.empty() && !ParseKey(keyName, settings.root)) {
		std::fprintf(stderr, "wav2ftm: unknown key '%s'\n", keyName.c_str());
		return 2;
	}
	settings.grid.tempo = bpm;	// speed 6 at 4 rows per beat: BPM = tempo * 6 / speed
	settings.grid.speed = 6;
	settings.quantize.alignToStart = !keepSilence;
	settings.quantize.minRestSeconds = minRestMs / 1000.0;
	settings.print = print;

	TextModuleOptions options;
	options.expansion = expansion;
	options.melodyChannel = channel;
	options.patternLength = patternLength;
	options.grid = settings.grid;
	options.title = title.empty() ? BaseName(input.empty() ? output : input) : title;

	std::vector<MelodyPart> parts;
	std::vector<GridHit> drums;
	std::string sources;
	auto addSource = [&](const std::string &part, const std::string &file) {
		sources += (sources.empty() ? "" : ", ") + part + " from " + BaseName(file);
	};

	if (haveMelody) {
		MelodyPart lead;
		lead.channel = channel;
		const bool low = (channel == 2 || channel == 7);	// triangle and sawtooth suit bass lines
		if (!TranscribeMelody(input, "melody", low ? 48 : 67, haveTranspose ? &transpose : nullptr, settings, lead.notes))
			return 1;
		parts.push_back(lead);
		addSource("melody", input);
	}
	const std::string drumsPath = (mode == "drums" && !input.empty()) ? input : drumsFile;
	if (!drumsPath.empty()) {
		if (!TranscribeDrums(drumsPath, settings, drums))
			return 1;
		addSource("drums", drumsPath);
	}
	if (!bassFile.empty()) {
		MelodyPart bass;
		bass.channel = bassChannel;
		bass.instrument = 4;
		bass.name = "Bass";
		if (!TranscribeMelody(bassFile, "bass", 48, nullptr, settings, bass.notes))
			return 1;
		parts.push_back(bass);
		addSource("bass", bassFile);
	}
	options.comment = "Transcribed by wav2ftm at " + std::to_string(bpm) + " BPM: " + sources;

	std::string error;
	const std::string text = WriteTextModule(parts, drums, options);
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

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

#include "Midi.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace nsf {

namespace {

// General MIDI's programs. Program 115 is "Steel Drums" there: called the
// steel pan here, so that its name doesn't make its notes drums (DrumOfName)
const char *const PROGRAMS[128] = {
	"Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano", "Electric Piano 1",
	"Electric Piano 2", "Harpsichord", "Clavinet", "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone",
	"Tubular Bells", "Dulcimer", "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion",
	"Harmonica", "Tango Accordion", "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)",
	"Electric Guitar (clean)", "Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
	"Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)", "Fretless Bass", "Slap Bass 1", "Slap Bass 2",
	"Synth Bass 1", "Synth Bass 2", "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings",
	"Orchestral Harp", "Timpani", "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs",
	"Voice Oohs", "Synth Voice", "Orchestra Hit", "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section",
	"Synth Brass 1", "Synth Brass 2", "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon",
	"Clarinet", "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
	"Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)", "Lead 5 (charang)", "Lead 6 (voice)",
	"Lead 7 (fifths)", "Lead 8 (bass and lead)", "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)",
	"Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)", "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)",
	"FX 4 (atmosphere)", "FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)", "Sitar", "Banjo", "Shamisen",
	"Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai", "Tinkle Bell", "Agogo", "Steel Pan", "Woodblock", "Taiko Drum",
	"Melodic Tom", "Synth Drum", "Reverse Cymbal", "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
	"Telephone Ring", "Helicopter", "Applause", "Gunshot"
};

// General MIDI's drums: keys 35-81 on channel 10
const char *const DRUMS[47] = {
	"Acoustic Bass Drum", "Bass Drum 1", "Side Stick", "Acoustic Snare", "Hand Clap", "Electric Snare", "Low Floor Tom",
	"Closed Hi-Hat", "High Floor Tom", "Pedal Hi-Hat", "Low Tom", "Open Hi-Hat", "Low-Mid Tom", "Hi-Mid Tom", "Crash Cymbal 1",
	"High Tom", "Ride Cymbal 1", "Chinese Cymbal", "Ride Bell", "Tambourine", "Splash Cymbal", "Cowbell", "Crash Cymbal 2",
	"Vibraslap", "Ride Cymbal 2", "Hi Bongo", "Low Bongo", "Mute Hi Conga", "Open Hi Conga", "Low Conga", "High Timbale",
	"Low Timbale", "High Agogo", "Low Agogo", "Cabasa", "Maracas", "Short Whistle", "Long Whistle", "Short Guiro", "Long Guiro",
	"Claves", "Hi Wood Block", "Low Wood Block", "Mute Cuica", "Open Cuica", "Mute Triangle", "Open Triangle"
};

const int DRUM_INSTRUMENTS = MIDI_DRUM_INSTRUMENTS;
const int OTHER_DRUM = DRUM_INSTRUMENTS + 47;
const int DRUM_CHANNEL = 9;				// MIDI's channel 10
const int MAX_VOICES = 6;				// notes at once a part keeps
const int MAX_DRUM_VOICES = 2;			// hits at once a kind of drum keeps
const int MAX_CHANNELS = 32;
const size_t MAX_EVENTS = 2000000;
const size_t MAX_NOTES = 200000;
const long MAX_ROWS = 16384;
const unsigned long MAX_TICK = 0x7FFFFFFFUL;
const int DEFAULT_TEMPO = 500000;		// microseconds a quarter note: 120 BPM

enum { EV_ON, EV_OFF, EV_PROGRAM, EV_CONTROL, EV_TEMPO, EV_METER, EV_LOOP, EV_LOOP_END, EV_TRACK_END };

struct Event {
	unsigned long Tick;
	int Kind;
	int Track, Channel;
	int A, B;
};

struct Played {
	unsigned long Start, End;
	int Track, Channel, Key, Program;
	int Volume;			// 0-64: its velocity at its channel's volume and expression
	long Row, EndRow;
};

struct MeterAt {
	unsigned long Tick;
	int Num, Den;
};

unsigned long Big32(const std::vector<uint8_t> &b, size_t At)
{
	return (static_cast<unsigned long>(b[At]) << 24) | (static_cast<unsigned long>(b[At + 1]) << 16) |
		(static_cast<unsigned long>(b[At + 2]) << 8) | static_cast<unsigned long>(b[At + 3]);
}

int Big16(const std::vector<uint8_t> &b, size_t At)
{
	return (b[At] << 8) | b[At + 1];
}

// The bytes of a track, read one at a time
struct Reader {
	const std::vector<uint8_t> &B;
	size_t Pos, End;
	bool Bad;
	Reader(const std::vector<uint8_t> &Bytes, size_t From, size_t To) : B(Bytes), Pos(From), End(To), Bad(false) {}
	int Byte()
	{
		if (Pos >= End) {
			Bad = true;
			return 0;
		}
		return B[Pos++];
	}
	unsigned long Number()		// a variable-length quantity
	{
		unsigned long Value = 0;
		for (int i = 0; i < 4; ++i) {
			const int c = Byte();
			Value = (Value << 7) | static_cast<unsigned long>(c & 0x7F);
			if ((c & 0x80) == 0)
				return Value;
		}
		Bad = true;
		return Value;
	}
};

// Printable ASCII of a text event, trimmed
std::string Text(const std::vector<uint8_t> &b, size_t At, size_t Length)
{
	std::string Out;
	for (size_t i = 0; i < Length; ++i)
		if (b[At + i] >= 32 && b[At + i] < 127)
			Out += static_cast<char>(b[At + i]);
	const size_t First = Out.find_first_not_of(' '), Last = Out.find_last_not_of(' ');
	return First == std::string::npos ? std::string() : Out.substr(First, Last - First + 1);
}

// Its letters, in lower case
std::string Letters(const std::string &s)
{
	std::string Out;
	for (size_t i = 0; i < s.size(); ++i)
		if (std::isalpha(static_cast<unsigned char>(s[i])))
			Out += static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
	return Out;
}

// A name a sequencer gives by itself ("Track 3", "Untitled"), that says nothing
bool PlainName(const std::string &Name)
{
	static const char *const PLAIN[] = { "", "track", "untitled", "untitledtrack", "newtrack", "instrument", "channel", "midi",
		"midichannel", "midiout", "part", "seq", "sequence", "tempo", "tempotrack", "conductor", "conductortrack", "control",
		"setup", "system", "noname", "song", "title" };
	const std::string l = Letters(Name);
	for (size_t i = 0; i < sizeof(PLAIN) / sizeof(PLAIN[0]); ++i)
		if (l == PLAIN[i])
			return true;
	return false;
}

// The instrument a drum key plays, named for the kind of drum it is
int DrumInstrument(int Key)
{
	return Key >= 35 && Key <= 81 ? DRUM_INSTRUMENTS + Key - 35 : OTHER_DRUM;
}

std::string DrumInstrumentName(int Index)
{
	if (Index >= OTHER_DRUM)
		return "Percussion";
	const std::string Name = DRUMS[Index - DRUM_INSTRUMENTS];
	if (!DrumOfName(Name).empty())
		return Name;
	// The side stick plays the snare's part
	return Name + (Index - DRUM_INSTRUMENTS + 35 == 37 ? " (snare rim)" : " percussion");
}

// FamiTracker's speed and tempo for Bpm (a quarter note) at PerQuarter rows
// a quarter: a row lasts 2.5 / Tempo * Speed seconds
void SpeedAndTempo(double Bpm, int PerQuarter, int &Speed, int &Tempo)
{
	Speed = std::max(1, 24 / PerQuarter);
	double T = Bpm * PerQuarter * Speed / 24.0;
	while (T > 255.0 && Speed > 1)
		T = Bpm * PerQuarter * --Speed / 24.0;
	while (T < 32.0 && Speed < 31)
		T = Bpm * PerQuarter * ++Speed / 24.0;
	Tempo = std::max(32, std::min(255, static_cast<int>(std::floor(T + 0.5))));
}

std::string Count(long n, const char *One, const char *Many)
{
	return std::to_string(n) + " " + (n == 1 ? One : Many);
}

std::string BpmText(double b)
{
	char Buf[32];
	std::snprintf(Buf, sizeof(Buf), "%.0f", b);
	return Buf;
}

}	// namespace

const char *MidiProgramName(int Program)
{
	return PROGRAMS[std::max(0, std::min(127, Program))];
}

const char *MidiDrumName(int Key)
{
	return Key >= 35 && Key <= 81 ? DRUMS[Key - 35] : 0;
}

bool IsMidi(const std::vector<uint8_t> &Bytes)
{
	if (Bytes.size() >= 4 && !std::memcmp(&Bytes[0], "MThd", 4))
		return true;
	return Bytes.size() >= 12 && !std::memcmp(&Bytes[0], "RIFF", 4) && !std::memcmp(&Bytes[8], "RMID", 4);
}

bool ParseMidi(const std::vector<uint8_t> &Bytes, TrackerSong &Out, std::string &Error)
{
	Out = TrackerSong();

	// A RIFF file (.rmi) holds the MIDI file in its "data" chunk
	size_t Begin = 0, Finish = Bytes.size();
	if (Bytes.size() >= 12 && !std::memcmp(&Bytes[0], "RIFF", 4) && !std::memcmp(&Bytes[8], "RMID", 4)) {
		size_t At = 12;
		bool Found = false;
		while (At + 8 <= Bytes.size() && !Found) {
			const unsigned long Size = static_cast<unsigned long>(Bytes[At + 4]) | (static_cast<unsigned long>(Bytes[At + 5]) << 8) |
				(static_cast<unsigned long>(Bytes[At + 6]) << 16) | (static_cast<unsigned long>(Bytes[At + 7]) << 24);
			if (!std::memcmp(&Bytes[At], "data", 4)) {
				Begin = At + 8;
				Finish = Size < Bytes.size() - Begin ? Begin + Size : Bytes.size();
				Found = true;
			}
			else if (Size > Bytes.size() - At - 8)
				break;
			else
				At += 8 + Size + (Size & 1);
		}
		if (!Found) {
			Error = "a RIFF MIDI file without its music";
			return false;
		}
	}
	if (Finish - Begin < 14 || std::memcmp(&Bytes[Begin], "MThd", 4) != 0) {
		Error = "not a MIDI file";
		return false;
	}
	const unsigned long HeaderSize = Big32(Bytes, Begin + 4);
	if (HeaderSize < 6 || HeaderSize > Finish - Begin - 8) {
		Error = "a MIDI file with a broken header";
		return false;
	}
	const int Format = Big16(Bytes, Begin + 8), Division = Big16(Bytes, Begin + 12);
	double PerQuarter;	// ticks a quarter note
	bool Smpte = false;
	if (Division & 0x8000) {
		// Frames of a clock: the music in seconds, taken as beats at 120 BPM
		PerQuarter = (256 - (Division >> 8)) * (Division & 0xFF) * 0.5;
		Smpte = true;
	}
	else
		PerQuarter = Division;
	if (PerQuarter <= 0) {
		Error = "a MIDI file without timing";
		return false;
	}

	// Every track's events at their ticks
	std::vector<Event> Events;
	std::vector<std::string> TrackNames, InstrumentNames;
	std::string Copyright;
	size_t At = Begin + 8 + HeaderSize;
	int Tracks = 0;
	bool Broken = false;
	while (At + 8 <= Finish && Tracks < 256 && Events.size() < MAX_EVENTS) {
		const unsigned long Size = Big32(Bytes, At + 4);
		const size_t Start = At + 8, Stop = Size < Finish - Start ? Start + Size : Finish;
		const bool IsTrack = !std::memcmp(&Bytes[At], "MTrk", 4);
		At = Stop;
		if (!IsTrack)
			continue;
		const int Track = Tracks++;
		TrackNames.push_back(std::string());
		InstrumentNames.push_back(std::string());
		Reader R(Bytes, Start, Stop);
		unsigned long Tick = 0;
		int Status = 0;
		while (R.Pos < R.End && Events.size() < MAX_EVENTS) {
			Tick += R.Number();
			int Byte = R.Byte();
			if (R.Bad || Tick > MAX_TICK) {
				R.Bad = true;
				break;
			}
			int Data1 = -1;
			if (Byte < 0x80) {
				// Running status: the last channel message's again
				if (Status < 0x80) {
					R.Bad = true;
					break;
				}
				Data1 = Byte;
				Byte = Status;
			}
			else if (Byte < 0xF0)
				Status = Byte;
			Event E;
			E.Tick = Tick;
			E.Kind = EV_ON;
			E.Track = Track;
			E.Channel = Byte & 0x0F;
			E.A = 0;
			E.B = 0;
			if (Byte == 0xFF) {
				// A meta event: the tempo, the meter, names and markers; the track's end
				const int Meta = R.Byte();
				const unsigned long Length = R.Number();
				if (R.Bad || Length > R.End - R.Pos) {
					R.Bad = true;
					break;
				}
				const size_t Data = R.Pos;
				R.Pos += Length;
				if (Meta == 0x2F) {
					E.Kind = EV_TRACK_END;
					Events.push_back(E);
					break;
				}
				if (Meta == 0x51 && Length >= 3) {
					E.Kind = EV_TEMPO;
					E.A = (Bytes[Data] << 16) | (Bytes[Data + 1] << 8) | Bytes[Data + 2];
					if (E.A > 0 && !Smpte)
						Events.push_back(E);
				}
				else if (Meta == 0x58 && Length >= 2) {
					E.Kind = EV_METER;
					E.A = Bytes[Data];
					E.B = Bytes[Data + 1];
					if (E.A > 0 && E.B <= 6)
						Events.push_back(E);
				}
				else if (Meta >= 0x01 && Meta <= 0x07) {
					const std::string Words = Text(Bytes, Data, Length);
					if (Meta == 0x03 && TrackNames[Track].empty())
						TrackNames[Track] = Words;
					else if (Meta == 0x04 && InstrumentNames[Track].empty())
						InstrumentNames[Track] = Words;
					else if (Meta == 0x02 && Copyright.empty())
						Copyright = Words;
					else if (Meta == 0x01 || Meta == 0x06 || Meta == 0x07) {
						// Markers of a loop: "loopStart" and "loopEnd", or [ and ]
						const std::string l = Letters(Words);
						if (l == "loopstart" || l == "loop" || Words == "[") {
							E.Kind = EV_LOOP;
							Events.push_back(E);
						}
						else if (l == "loopend" || Words == "]") {
							E.Kind = EV_LOOP_END;
							Events.push_back(E);
						}
					}
				}
				continue;
			}
			if (Byte == 0xF0 || Byte == 0xF7) {
				// System exclusive: skipped
				const unsigned long Length = R.Number();
				if (R.Bad || Length > R.End - R.Pos) {
					R.Bad = true;
					break;
				}
				R.Pos += Length;
				continue;
			}
			if (Byte >= 0xF0) {
				// System messages a file shouldn't hold: their data skipped
				const int Skip = Byte == 0xF2 ? 2 : (Byte == 0xF1 || Byte == 0xF3) ? 1 : 0;
				for (int i = 0; i < Skip; ++i)
					R.Byte();
				if (R.Bad)
					break;
				continue;
			}
			const int Type = Byte & 0xF0;
			if (Data1 < 0)
				Data1 = R.Byte();
			const int Data2 = Type == 0xC0 || Type == 0xD0 ? 0 : R.Byte();
			if (R.Bad)
				break;
			E.A = Data1 & 0x7F;
			E.B = Data2 & 0x7F;
			if (Type == 0x90 && E.B > 0)
				E.Kind = EV_ON;
			else if (Type == 0x80 || Type == 0x90)
				E.Kind = EV_OFF;	// a note on at velocity 0 ends it too
			else if (Type == 0xC0)
				E.Kind = EV_PROGRAM;
			else if (Type == 0xB0 && (E.A == 111 || E.A == 116 || E.A == 118))
				E.Kind = EV_LOOP;	// RPG Maker's loop point; EMIDI's loop starts
			else if (Type == 0xB0 && (E.A == 117 || E.A == 119))
				E.Kind = EV_LOOP_END;
			else if (Type == 0xB0 && (E.A == 7 || E.A == 11 || E.A == 121))
				E.Kind = EV_CONTROL;	// volume, expression, all controllers reset
			else
				continue;
			Events.push_back(E);
		}
		Broken = Broken || R.Bad;
	}
	if (Tracks == 0) {
		Error = "a MIDI file without tracks";
		return false;
	}
	// In time; at one tick, the tracks in order and each track's events as written
	std::stable_sort(Events.begin(), Events.end(), [](const Event &a, const Event &b) { return a.Tick < b.Tick; });

	// The notes, each from its note on to its note off
	std::vector<Played> Notes;
	std::vector<std::pair<unsigned long, int> > Tempos;	// tick, microseconds a quarter note
	std::vector<MeterAt> Meters;
	int Programs[16], Volumes[16], Expressions[16];
	for (int c = 0; c < 16; ++c) {
		Programs[c] = 0;
		Volumes[c] = 100;
		Expressions[c] = 127;
	}
	long LoopTick = -1, EndTick = -1;
	unsigned long LastTick = 0, TrackEnd = 0;
	std::map<std::pair<int, int>, std::vector<size_t> > Sounding;	// (track and channel, key) -> notes not ended yet, oldest first
	// Note offs with no note to end (the tick, and how many): a note on the
	// same tick after one is a note of no length (a grace note) written end first
	std::map<std::pair<int, int>, std::pair<unsigned long, int> > Unmatched;
	for (size_t i = 0; i < Events.size(); ++i) {
		const Event &E = Events[i];
		const std::pair<int, int> Key(E.Track * 16 + E.Channel, E.A);
		switch (E.Kind) {
		case EV_ON: {
			LastTick = std::max(LastTick, E.Tick);
			std::map<std::pair<int, int>, std::pair<unsigned long, int> >::iterator u = Unmatched.find(Key);
			const bool Ended = u != Unmatched.end() && u->second.first == E.Tick && u->second.second > 0;
			if (Ended)
				--u->second.second;
			if (Notes.size() < MAX_NOTES) {
				Played P;
				P.Start = P.End = E.Tick;
				P.Track = E.Track;
				P.Channel = E.Channel;
				P.Key = E.A;
				P.Program = Programs[E.Channel];
				// Its loudness: the velocity, at the channel's volume (100 as General MIDI starts it) and expression
				const double Level = E.B / 127.0 * Volumes[E.Channel] / 100.0 * Expressions[E.Channel] / 127.0;
				P.Volume = std::max(0, std::min(64, static_cast<int>(std::floor(Level * 64 + 0.5))));
				P.Row = P.EndRow = 0;
				if (!Ended)
					Sounding[Key].push_back(Notes.size());
				Notes.push_back(P);
			}
			break;
		}
		case EV_OFF: {
			LastTick = std::max(LastTick, E.Tick);
			std::map<std::pair<int, int>, std::vector<size_t> >::iterator it = Sounding.find(Key);
			if (it != Sounding.end() && !it->second.empty()) {
				Notes[it->second.front()].End = E.Tick;
				it->second.erase(it->second.begin());
			}
			else {
				std::pair<unsigned long, int> &u = Unmatched[Key];
				if (u.first == E.Tick && u.second > 0)
					++u.second;
				else
					u = std::make_pair(E.Tick, 1);
			}
			break;
		}
		case EV_PROGRAM:
			Programs[E.Channel] = E.A;
			break;
		case EV_CONTROL:
			if (E.A == 7)
				Volumes[E.Channel] = E.B;
			else if (E.A == 11)
				Expressions[E.Channel] = E.B;
			else
				Expressions[E.Channel] = 127;
			break;
		case EV_TEMPO:
			Tempos.push_back(std::make_pair(E.Tick, E.A));
			break;
		case EV_METER: {
			const MeterAt M = { E.Tick, E.A, 1 << E.B };
			Meters.push_back(M);
			break;
		}
		case EV_LOOP:
			if (LoopTick < 0)
				LoopTick = static_cast<long>(E.Tick);
			break;
		case EV_LOOP_END:
			// Only after where the loop starts
			if (EndTick < 0 && LoopTick >= 0 && static_cast<long>(E.Tick) > LoopTick)
				EndTick = static_cast<long>(E.Tick);
			break;
		case EV_TRACK_END:
			TrackEnd = std::max(TrackEnd, E.Tick);
			break;
		}
	}
	// Notes never ended last to the end; notes of no length (grace notes)
	// are left out, but drums; the loop's end ends the song
	for (std::map<std::pair<int, int>, std::vector<size_t> >::const_iterator it = Sounding.begin(); it != Sounding.end(); ++it)
		for (size_t k = 0; k < it->second.size(); ++k)
			Notes[it->second[k]].End = std::max(LastTick, TrackEnd);
	{
		std::vector<Played> Kept;
		for (size_t i = 0; i < Notes.size(); ++i) {
			if (Notes[i].End <= Notes[i].Start && Notes[i].Channel != DRUM_CHANNEL)
				continue;
			if (EndTick > 0 && Notes[i].Start >= static_cast<unsigned long>(EndTick))
				continue;
			Kept.push_back(Notes[i]);
			if (EndTick > 0)
				Kept.back().End = std::min(Kept.back().End, static_cast<unsigned long>(EndTick));
		}
		Notes.swap(Kept);
	}
	if (Notes.empty()) {
		Error = Broken ? "a broken MIDI file without notes" : "a MIDI file without notes";
		return false;
	}
	const unsigned long SongEnd = EndTick > 0 ? static_cast<unsigned long>(EndTick) : std::max(LastTick, TrackEnd);

	// The meter: the one the song keeps longest, from where its longest stretch starts
	int Num = 4, Den = 4;
	unsigned long MeterTick = 0;
	if (!Meters.empty()) {
		std::map<std::pair<int, int>, unsigned long> Lasts;
		for (size_t i = 0; i < Meters.size(); ++i) {
			const unsigned long Until = std::max(Meters[i].Tick, i + 1 < Meters.size() ? Meters[i + 1].Tick : SongEnd);
			Lasts[std::make_pair(Meters[i].Num, Meters[i].Den)] += Until - Meters[i].Tick;
		}
		unsigned long Most = 0;
		for (size_t i = 0; i < Meters.size(); ++i) {
			const unsigned long l = Lasts[std::make_pair(Meters[i].Num, Meters[i].Den)];
			if (i == 0 || l > Most) {
				Most = l;
				Num = Meters[i].Num;
				Den = Meters[i].Den;
			}
		}
		unsigned long Longest = 0;
		bool Have = false;
		for (size_t i = 0; i < Meters.size(); ) {
			size_t j = i + 1;
			while (j < Meters.size() && Meters[j].Num == Meters[i].Num && Meters[j].Den == Meters[i].Den)
				++j;
			if (Meters[i].Num == Num && Meters[i].Den == Den) {
				const unsigned long Until = std::max(Meters[i].Tick, j < Meters.size() ? Meters[j].Tick : SongEnd);
				if (!Have || Until - Meters[i].Tick > Longest) {
					Have = true;
					Longest = Until - Meters[i].Tick;
					MeterTick = Meters[i].Tick;
				}
			}
			i = j;
		}
	}

	// The grid: rows a quarter note, the coarsest the notes start on (within
	// 15% of a row); for music played rather than written, sixteenths
	int Grid = 4;
	{
		static const int GRIDS[4] = { 4, 6, 8, 12 };
		double Fit[4], Best = 0;
		for (int g = 0; g < 4; ++g) {
			size_t On = 0;
			for (size_t i = 0; i < Notes.size(); ++i) {
				const double Pos = Notes[i].Start * double(GRIDS[g]) / PerQuarter;
				if (std::fabs(Pos - std::floor(Pos + 0.5)) <= 0.15)
					++On;
			}
			Fit[g] = double(On) / Notes.size();
			Best = std::max(Best, Fit[g]);
		}
		int Chosen = -1;
		for (int g = 0; g < 4 && Chosen < 0; ++g)
			if (Fit[g] >= 0.95)
				Chosen = g;
		for (int g = 0; g < 4 && Chosen < 0 && Best >= 0.6; ++g)
			if (Fit[g] >= Best - 0.05)
				Chosen = g;
		Grid = GRIDS[std::max(0, Chosen)];
	}

	// The beat and the bar the study counts in: quarter notes, the dotted
	// quarters of 6/8, 9/8 and 12/8, the eighths of 5/8 and 7/8
	int BeatRows = Grid, BeatsPerBar = 4;
	std::string Meter;
	bool OddMeter = false;
	if (!Meters.empty()) {
		if (Den == 8 && Num % 3 == 0) {
			BeatsPerBar = Num / 3;
			BeatRows = Grid * 3 / 2;
		}
		else if (Num * 4 % Den == 0)
			BeatsPerBar = Num * 4 / Den;
		else if (Den == 8) {
			BeatsPerBar = Num;
			BeatRows = Grid / 2;
		}
		else
			BeatsPerBar = 0;
		if (BeatsPerBar >= 1 && BeatsPerBar <= 12)
			Meter = std::to_string(Num) + "/" + std::to_string(Den);
		else {
			BeatsPerBar = 4;
			BeatRows = Grid;
			OddMeter = true;
		}
	}
	const long BarRows = static_cast<long>(BeatRows) * BeatsPerBar;

	// The tempo: the one the song keeps longest (120 BPM until the file says)
	std::vector<std::pair<unsigned long, int> > AllTempos;
	if (Tempos.empty() || Tempos[0].first > 0)
		AllTempos.push_back(std::make_pair(0UL, DEFAULT_TEMPO));
	AllTempos.insert(AllTempos.end(), Tempos.begin(), Tempos.end());
	int MainTempo = DEFAULT_TEMPO, Changes = 0;
	double Slowest = 0, Fastest = 0;
	{
		std::map<int, unsigned long> Lasts;
		for (size_t i = 0; i < AllTempos.size(); ++i) {
			const unsigned long Until = std::max(AllTempos[i].first, i + 1 < AllTempos.size() ? AllTempos[i + 1].first : SongEnd);
			Lasts[AllTempos[i].second] += Until - AllTempos[i].first;
		}
		unsigned long Most = 0;
		for (size_t i = 0; i < AllTempos.size(); ++i) {
			const unsigned long l = Lasts[AllTempos[i].second];
			if (i == 0 || l > Most) {
				Most = l;
				MainTempo = AllTempos[i].second;
			}
			if (i == 0 || AllTempos[i].first < SongEnd) {
				const double b = 60000000.0 / AllTempos[i].second;
				Slowest = i == 0 ? b : std::min(Slowest, b);
				Fastest = i == 0 ? b : std::max(Fastest, b);
				if (i > 0 && AllTempos[i].second != AllTempos[i - 1].second)
					++Changes;
			}
		}
	}
	const double MainBpm = 60000000.0 / MainTempo;
	SpeedAndTempo(MainBpm, Grid, Out.Speed, Out.Tempo);

	// Rows
	auto RowOf = [&](unsigned long Tick) -> long {
		const double r = std::floor(Tick * double(Grid) / PerQuarter + 0.5);
		return r > double(MAX_ROWS) ? MAX_ROWS + 1 : static_cast<long>(r);
	};
	long NotesEnd = 1;
	for (size_t i = 0; i < Notes.size(); ++i) {
		Notes[i].Row = RowOf(Notes[i].Start);
		Notes[i].EndRow = std::max(Notes[i].Row + 1, RowOf(Notes[i].End));
		NotesEnd = std::max(NotesEnd, Notes[i].EndRow);
	}
	const long Phase = Meters.empty() ? 0 : RowOf(MeterTick) % BarRows;
	long Rows;
	if (EndTick > 0)
		Rows = std::max(1L, RowOf(static_cast<unsigned long>(EndTick)));
	else {
		// To the bar line after the last note, or to the tracks' end on a bar
		// line soon after
		const long TrackRows = RowOf(TrackEnd);
		const long BarEnd = NotesEnd <= Phase ? NotesEnd : Phase + (NotesEnd - Phase + BarRows - 1) / BarRows * BarRows;
		const bool OnBar = TrackRows >= Phase && (TrackRows - Phase) % BarRows == 0;
		Rows = OnBar && TrackRows >= NotesEnd && TrackRows <= NotesEnd + 2 * BarRows ? TrackRows : BarEnd;
	}
	Rows = std::min(Rows, MAX_ROWS);
	long LoopRow = LoopTick >= 0 ? RowOf(static_cast<unsigned long>(LoopTick)) : -1;
	if (LoopRow >= Rows)
		LoopRow = -1;

	// The parts (a track's MIDI channel), each on channels of one note at a
	// time: the notes that start together go on the free ones in order, the
	// highest on the first, so that a melody stays on one and a chord's top
	// note leads; the drums on a channel for each kind
	struct Voice {
		int Track, Channel;
		std::string Kind;			// the drums': the kind of drum
		bool First;					// its part's first (or its kind's)
		std::vector<size_t> Notes;
		long Free;					// the row it's free from
		std::string Name;
	};
	std::vector<Voice> Voices;
	long LeftOut = 0;
	const long Smear = std::max(1, Grid / 4);	// a note that ends a sixteenth or less into the next is legato
	std::map<std::pair<int, int>, std::vector<size_t> > Parts;
	for (size_t i = 0; i < Notes.size(); ++i)
		if (Notes[i].Row < Rows)
			Parts[std::make_pair(Notes[i].Track, Notes[i].Channel)].push_back(i);
	for (std::map<std::pair<int, int>, std::vector<size_t> >::iterator it = Parts.begin(); it != Parts.end(); ++it) {
		std::vector<size_t> &Mine = it->second;
		const bool Drums = it->first.second == DRUM_CHANNEL;
		std::stable_sort(Mine.begin(), Mine.end(), [&](size_t a, size_t b) {
			return Notes[a].Row != Notes[b].Row ? Notes[a].Row < Notes[b].Row : Notes[a].Key > Notes[b].Key;
		});
		std::map<std::string, std::vector<size_t> > Kinds;	// voices by the kind of drum; the notes are one kind
		for (size_t k = 0; k < Mine.size(); ) {
			// The notes that start on this row, highest first
			const long Row = Notes[Mine[k]].Row;
			size_t End = k;
			while (End < Mine.size() && Notes[Mine[End]].Row == Row)
				++End;
			std::map<std::string, std::vector<std::pair<size_t, bool> > > FreeOf;	// by kind: voices free here (and whether legato)
			std::map<std::string, size_t> Taken;
			for (size_t j = k; j < End; ++j) {
				const Played &P = Notes[Mine[j]];
				if (j > k && Notes[Mine[j - 1]].Key == P.Key)
					continue;	// the same key twice on a row: once
				const std::string Kind = Drums ? DrumOfName(DrumInstrumentName(DrumInstrument(P.Key))) : std::string();
				std::vector<size_t> &Own = Kinds[Kind];
				if (!FreeOf.count(Kind)) {
					// Free, or holding a note a sixteenth or less into this one (legato played a little over)
					std::vector<std::pair<size_t, bool> > &F = FreeOf[Kind];
					for (size_t v = 0; v < Own.size(); ++v) {
						const Voice &V = Voices[Own[v]];
						const bool Free = V.Free <= Row;
						const bool Legato = !Free && !Drums && V.Free - Row <= Smear && Notes[V.Notes.back()].Row < Row;
						if (Free || Legato)
							F.push_back(std::make_pair(Own[v], Legato));
					}
				}
				const std::vector<std::pair<size_t, bool> > &F = FreeOf[Kind];
				size_t &Used = Taken[Kind];
				size_t Index;
				if (Used < F.size()) {
					Index = F[Used].first;
					if (F[Used].second)
						Notes[Voices[Index].Notes.back()].EndRow = Row;	// it held on into this one
					++Used;
				}
				else if (Own.size() >= size_t(Drums ? MAX_DRUM_VOICES : MAX_VOICES)) {
					++LeftOut;
					continue;
				}
				else {
					Voice V;
					V.Track = it->first.first;
					V.Channel = it->first.second;
					V.Kind = Kind;
					V.First = Own.empty();
					V.Free = 0;
					Index = Voices.size();
					Own.push_back(Index);
					Voices.push_back(V);
				}
				Voice &V = Voices[Index];
				V.Notes.push_back(Mine[j]);
				V.Free = Drums ? Row + 1 : P.EndRow;
			}
			k = End;
		}
	}
	// At most MAX_CHANNELS: the voices with the fewest notes go, the parts' first ones last
	while (Voices.size() > size_t(MAX_CHANNELS)) {
		size_t Drop = 0;
		for (size_t v = 1; v < Voices.size(); ++v)
			if (Voices[v].First < Voices[Drop].First ||
				(Voices[v].First == Voices[Drop].First && Voices[v].Notes.size() < Voices[Drop].Notes.size()))
				Drop = v;
		LeftOut += static_cast<long>(Voices[Drop].Notes.size());
		Voices.erase(Voices.begin() + static_cast<long>(Drop));
	}

	// The channels' names: the track's (or its instrument's) when the track
	// plays one part, else the program's; parts of one name told apart by
	// their MIDI channel, or else their track; the drums with their kind; a
	// part's voices numbered from the first: "Violin 2 (track 3)"
	const std::string Title = !TrackNames.empty() && !PlainName(TrackNames[0]) ? TrackNames[0] : std::string();
	{
		std::map<int, std::set<int> > PartsOfTrack;
		for (size_t v = 0; v < Voices.size(); ++v)
			PartsOfTrack[Voices[v].Track].insert(Voices[v].Channel);
		std::map<int, std::string> Bases;	// track and channel -> name
		std::map<std::string, std::set<std::pair<int, int> > > Named;
		for (size_t v = 0; v < Voices.size(); ++v) {
			const Voice &V = Voices[v];
			const size_t t = static_cast<size_t>(V.Track);
			// Track 0's name is the song's when other tracks follow
			const bool Alone = PartsOfTrack[V.Track].size() == 1 && !(V.Track == 0 && Tracks > 1);
			std::string Base;
			if (Alone && !PlainName(TrackNames[t]) && TrackNames[t] != Title)
				Base = TrackNames[t];
			else if (Alone && !PlainName(InstrumentNames[t]))
				Base = InstrumentNames[t];
			else if (V.Channel == DRUM_CHANNEL)
				Base = "Drums";
			else
				Base = MidiProgramName(Notes[V.Notes[0]].Program);
			if (!Bases.count(V.Track * 16 + V.Channel)) {
				Bases[V.Track * 16 + V.Channel] = Base;
				Named[Base].insert(std::make_pair(V.Track, V.Channel));
			}
		}
		std::map<int, std::string> Apart;	// track and channel -> " (channel 2)", " (track 3)"
		for (std::map<std::string, std::set<std::pair<int, int> > >::const_iterator it = Named.begin(); it != Named.end(); ++it) {
			if (it->second.size() < 2)
				continue;
			std::set<int> Channels;
			for (std::set<std::pair<int, int> >::const_iterator p = it->second.begin(); p != it->second.end(); ++p)
				Channels.insert(p->second);
			for (std::set<std::pair<int, int> >::const_iterator p = it->second.begin(); p != it->second.end(); ++p)
				Apart[p->first * 16 + p->second] = Channels.size() == it->second.size() ? " (channel " + std::to_string(p->second + 1) + ")"
					: " (track " + std::to_string(p->first + 1) + ")";
		}
		std::map<std::pair<int, std::string>, int> Number, Total;	// (track and channel, kind)
		for (size_t v = 0; v < Voices.size(); ++v)
			++Total[std::make_pair(Voices[v].Track * 16 + Voices[v].Channel, Voices[v].Kind)];
		for (size_t v = 0; v < Voices.size(); ++v) {
			Voice &V = Voices[v];
			const int Part = V.Track * 16 + V.Channel;
			V.Name = V.Channel == DRUM_CHANNEL ? Bases[Part] + ": " + V.Kind : Bases[Part];
			const std::pair<int, std::string> Key(Part, V.Kind);
			if (Total[Key] > 1)
				V.Name += " " + std::to_string(++Number[Key]);
			V.Name += Apart[Part];
		}
	}

	// The song
	Out.Format = "MIDI";
	Out.Title = Title;
	Out.Tracker = "format " + std::to_string(Format) + ", " + Count(Tracks, "track", "tracks");
	Out.Copyright = Copyright;
	Out.Meter = Meter;
	Out.Channels = static_cast<int>(Voices.size());
	Out.RowsPerBeat = BeatRows;
	Out.BeatsPerBar = BeatsPerBar;
	Out.FirstBarRow = Meters.empty() ? -1 : static_cast<int>(Phase);
	for (int p = 0; p < DRUM_INSTRUMENTS; ++p)
		Out.Instruments.push_back(PROGRAMS[p]);
	for (int d = DRUM_INSTRUMENTS; d <= OTHER_DRUM; ++d)
		Out.Instruments.push_back(DrumInstrumentName(d));
	for (size_t v = 0; v < Voices.size(); ++v)
		Out.ChannelNames.push_back(Voices[v].Name);
	std::vector<std::string> Said;
	if (Changes > 0)
		Said.push_back("The tempo changes " + Count(Changes, "time", "times") + " (" + BpmText(Slowest) + " to " + BpmText(Fastest) +
			" BPM); the study takes " + BpmText(MainBpm) + ", the tempo it keeps longest.");
	if (OddMeter)
		Said.push_back("It's in " + std::to_string(Num) + "/" + std::to_string(Den) + ", counted here in bars of 4/4.");
	if (LeftOut > 0)
		Said.push_back(Count(LeftOut, "note", "notes") + " left out: more than " + std::to_string(MAX_VOICES) +
			" at once in a part, or past " + std::to_string(MAX_CHANNELS) + " channels.");
	if (Broken)
		Said.push_back("The file is cut short or broken: this is what could be read.");
	for (size_t i = 0; i < Said.size(); ++i)
		Out.Message += (i ? " " : "") + Said[i];

	// Patterns of four bars from the first bar line; one starts where the song loops
	long PatternRows = BarRows * 4;
	while (PatternRows > 256 && PatternRows > BarRows)
		PatternRows -= BarRows;
	PatternRows = std::max(1L, std::min(PatternRows, 256L));
	std::set<long> Cuts;
	Cuts.insert(0);
	for (long r = Phase; r < Rows; r += PatternRows)
		Cuts.insert(r);
	if (LoopRow > 0)
		Cuts.insert(LoopRow);
	std::vector<long> Starts;
	for (std::set<long>::const_iterator c = Cuts.begin(); c != Cuts.end() && *c < Rows; ++c) {
		std::set<long>::const_iterator n = c;
		++n;
		const long End = n == Cuts.end() ? Rows : std::min(*n, Rows);
		for (long r = *c; r < End; r += 256)
			Starts.push_back(r);
	}
	Out.Restart = -1;
	for (size_t p = 0; p < Starts.size(); ++p) {
		const long End = p + 1 < Starts.size() ? Starts[p + 1] : Rows;
		if (Starts[p] == LoopRow)
			Out.Restart = static_cast<int>(p);
		TrackerPattern P;
		P.Rows = static_cast<int>(End - Starts[p]);
		P.Cells.assign(static_cast<size_t>(P.Rows), std::vector<TrackerCell>(Voices.size()));
		Out.Orders.push_back(static_cast<int>(p));
		Out.Patterns.push_back(P);
	}
	auto CellAt = [&](long Row, size_t Channel) -> TrackerCell & {
		const size_t p = static_cast<size_t>(std::upper_bound(Starts.begin(), Starts.end(), Row) - Starts.begin()) - 1;
		return Out.Patterns[p].Cells[static_cast<size_t>(Row - Starts[p])][Channel];
	};
	for (size_t v = 0; v < Voices.size(); ++v) {
		const std::vector<size_t> &Mine = Voices[v].Notes;
		for (size_t k = 0; k < Mine.size(); ++k) {
			const Played &P = Notes[Mine[k]];
			TrackerCell &C = CellAt(P.Row, v);
			C.Note = std::max(0, std::min(119, P.Key - 12));	// MIDI's middle C (60) is the trackers' C-4
			C.Instrument = P.Channel == DRUM_CHANNEL ? DrumInstrument(P.Key) : P.Program;
			C.Volume = P.Volume;
			// Its end, unless the next note starts there
			const bool Next = k + 1 < Mine.size() && Notes[Mine[k + 1]].Row <= P.EndRow;
			if (!Next && P.EndRow < Rows && P.Channel != DRUM_CHANNEL) {
				TrackerCell &Off = CellAt(P.EndRow, v);
				if (Off.Note == TRACKER_NONE)
					Off.Note = TRACKER_OFF;
			}
		}
	}
	// The tempo's changes, as tempo effects on the first channel
	if (!Voices.empty()) {
		auto TempoAt = [&](int Microseconds) {
			return std::max(32, std::min(255, static_cast<int>(std::floor(60000000.0 / Microseconds * Grid * Out.Speed / 24.0 + 0.5))));
		};
		std::map<long, int> Changed;
		int Now = TempoAt(DEFAULT_TEMPO);
		for (size_t t = 0; t < Tempos.size(); ++t) {
			const long Row = RowOf(Tempos[t].first);
			if (Row <= 0)
				Now = TempoAt(Tempos[t].second);
			else if (Row < Rows)
				Changed[Row] = TempoAt(Tempos[t].second);
		}
		for (std::map<long, int>::const_iterator it = Changed.begin(); it != Changed.end(); ++it) {
			if (it->second != Now) {
				TrackerCell &C = CellAt(it->first, 0);
				if (C.Effect == 0) {
					C.Effect = 'F';
					C.Param = it->second;
				}
			}
			Now = it->second;
		}
	}
	return true;
}

}	// namespace nsf

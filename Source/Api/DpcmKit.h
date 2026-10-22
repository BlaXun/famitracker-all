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

#pragma once

// A drum kit of samples for the DPCM channel, made here: a kick, a snare and
// two toms, synthesized and encoded as the 2A03's delta modulation (1-bit
// steps of 2 at 33.1 kHz). Sampled drums under the noise channel's hats are
// how many classic NES soundtracks get their punch (references action guide,
// topic drums); add_sample_drums adds the kit as an instrument and
// write_notes plays it by name on the DPCM channel.

#include <string>
#include <vector>

namespace ftapi {

struct DpcmSample {
	std::string Name;				// "Kit kick"
	std::vector<unsigned char> Data;	// 16n + 1 bytes, the DMC's own lengths
};

struct DpcmKey {
	std::string Drum;				// "kick"
	int Pitch;						// the note it's on, Octave * 12 + note (C-3 is 36)
	int Sample;						// in DpcmKit::Samples
	int Rate;						// the DMC's rate, 0-15 (15: 33.1 kHz)
	int Delta;						// the level it starts from, 0-127, -1 to leave it
};

struct DpcmKit {
	std::string Name;				// the instrument's: "Sample drums"
	std::vector<DpcmSample> Samples;
	std::vector<DpcmKey> Keys;
};

// The kit: kick on C-3, snare on D-3, low tom on F-3, high tom on A-3
const DpcmKit &SampleDrums();
// The key of a drum asked for by name ("kick", "snare", "low tom", "tom",
// "high tom", "bass drum"...); 0 when the kit has none
const DpcmKey *FindSampleDrum(const std::string &Name);

// The levels (0-127) a sound should follow at the DMC's 33144 Hz, as delta
// modulation from level Start: a bit a sample, the low bit first, padded to
// 16n + 1 bytes. It never steps past 0 or 127 (it holds there), so played
// from another level the sound walks the same way and ends where it started.
std::vector<unsigned char> EncodeDpcm(const std::vector<double> &Wave, int Start);
// The levels the DMC plays from Data, starting at Start (0-127)
std::vector<int> DecodeDpcm(const std::vector<unsigned char> &Data, int Start);

}	// namespace ftapi

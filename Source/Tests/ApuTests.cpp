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

// Tests for the stereo mixer (Source/APU/Mixer.cpp, CVRC7::EndFrame and the
// N163's one DAC; the 5B's channels go through it as the VRC6's do): every
// chip's channels, played by register writes the way a song plays them.
//
// With every channel in the middle, which is where a song that doesn't pan
// has them, both sides are the mono mix sample for sample: MONO holds hashes
// of what the mixer made before FamiTracker played in stereo (commit
// fd5f822; the 5B's from the mono code of commit 555233a, whose emulation of
// the chip replaced 0.4.6's). A channel panned all the way over is on that
// side alone, as loud as in the middle; halfway over, the other side has it at
// half its level.
//
// After a deliberate change to the emulation, the hashes are worked out again
// from the mono code before stereo (with that change made to it too), with
// the same scripts: configure with -DAPU_REFERENCE=<a folder holding that
// code's APU and Blip_Buffer folders>, and apu_tests prints them.
//
// Depth (the mixer window's Soft and Space, APU/Space.cpp): Soft and Space at
// 0, set explicitly or set and cleared before anything sounds, leave every
// chip's mix as it was, sample for sample; a channel in its own buffers with
// nothing to do there (Space without a room or an echo) sounds as it did,
// give or take the rounding of two buffers. Soft takes the designed share of
// a pulse's sound over 2 kHz off and keeps it brighter than a triangle; Space
// rings on after a note in each room, the same every time, unlike on the two
// sides, and its echo repeats when it should, each repeat quieter.
//
// Each rendering runs in a process of its own (fork), since the emulators
// keep some of their state in globals.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "APU/APU.h"
#include "APU/Types.h"

namespace {

int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

const int SAMPLE_RATE = 48000;
const int FRAMES = 40;
const int FRAME_SAMPLES = SAMPLE_RATE / 60;

enum chip_t { CHIP_2A03, CHIP_VRC6, CHIP_MMC5, CHIP_FDS, CHIP_N163, CHIP_VRC7, CHIP_S5B, CHIPS };
const char *const CHIP_NAMES[CHIPS] = { "2A03", "VRC6", "MMC5", "FDS", "N163", "VRC7", "5B" };
const uint8 CHIP_FLAGS[CHIPS] = { SNDCHIP_NONE, SNDCHIP_VRC6, SNDCHIP_MMC5, SNDCHIP_FDS, SNDCHIP_N163, SNDCHIP_VRC7, SNDCHIP_S5B };

// The channels a chip's script plays, by the bits of its mask (-1: none)
const int VOICES = 5;
const int CHANNELS_OF[CHIPS][VOICES] = {
	{ CHANID_SQUARE1, CHANID_SQUARE2, CHANID_TRIANGLE, CHANID_NOISE, CHANID_DPCM },
	{ CHANID_VRC6_PULSE1, CHANID_VRC6_PULSE2, CHANID_VRC6_SAWTOOTH, -1, -1 },
	{ CHANID_MMC5_SQUARE1, CHANID_MMC5_SQUARE2, -1, -1, -1 },
	{ CHANID_FDS, -1, -1, -1, -1 },
	// The chip's channels at 0x78, 0x70, 0x68 and 0x60, the four a song with four N163 channels uses
	{ CHANID_N163_CHAN8, CHANID_N163_CHAN7, CHANID_N163_CHAN6, CHANID_N163_CHAN5, -1 },
	{ CHANID_VRC7_CH1, CHANID_VRC7_CH2, CHANID_VRC7_CH3, -1, -1 },
	{ CHANID_S5B_CH1, CHANID_S5B_CH2, CHANID_S5B_CH3, -1, -1 },
};

const unsigned ALL = 0x1F;

#ifndef APU_REFERENCE
// MONO[chip]: the mono mixer's output for the chip's script with every channel playing
const uint64_t MONO[CHIPS] = {
	0x19cde0a3c8fd0947ULL,	// 2A03 (32006 samples, peak 20022)
	0x859d000517b13569ULL,	// VRC6 (32006 samples, peak 8101)
	0xc797d032f6fb808dULL,	// MMC5 (32006 samples, peak 7354)
	0xc991749aec17ea3bULL,	// FDS (32006 samples, peak 5882)
	0x1481cadf677ca954ULL,	// N163 (32006 samples, peak 8840)
	0x6e2ca61b182b3185ULL,	// VRC7 (32006 samples, peak 11708)
	0x94831a7e354267b1ULL,	// 5B (32006 samples, peak 5532)
};
#endif

// Single notes instead of a chip's script: a VRC6 pulse (50%) held at 291 Hz, the 2A03's triangle at the
// same pitch, and the pulse for 10 frames, then silence
enum tone_t { TONE_NONE, TONE_PULSE, TONE_TRIANGLE, TONE_BLIP };

struct Run {
	int Chip;
	unsigned Mask;			// the voices that play (CHANNELS_OF)
	int Pan[VOICES];		// their pans: 0x00 left, 0x80 the middle, 0xFF right
	int PanFrame;			// set before this frame's writes
	double Gain[VOICES];	// the mixer window's levels (1 as they play), set with the pans
	// Depth, when Depth: the voices' Soft and Space and the space's room, echo (seconds) and repeats, set
	// before DepthFrame's writes; Off: every voice's back to 0 before frame Off - 1 (0: never)
	bool Depth;
	int Soft[VOICES];
	int Space[VOICES];
	int Room;
	double Echo;
	int Repeats;
	int DepthFrame;
	int Off;
	int Tone;				// tone_t
	int Frames;				// 0: FRAMES
};

Run Plain(int Chip, unsigned Mask)
{
	Run R = Run();
	R.Chip = Chip;
	R.Mask = Mask;
	for (int v = 0; v < VOICES; ++v) {
		R.Pan[v] = 0x80;
		R.Gain[v] = 1.0;
	}
	return R;
}

Run Leveled(int Chip, unsigned Mask, double First)
{
	Run R = Plain(Chip, Mask);
	R.Gain[0] = First;
	return R;
}

Run Panned(int Chip, unsigned Mask, int First, int Second = 0x80, int Frame = 0)
{
	Run R = Plain(Chip, Mask);
	R.Pan[0] = First;
	R.Pan[1] = Second;
	R.PanFrame = Frame;
	return R;
}

Run Deep(int Chip, unsigned Mask, int Soft, int Space, int Room = 1, double Echo = 0.0, int Repeats = 3)
{
	Run R = Plain(Chip, Mask);
	R.Depth = true;
	R.Soft[0] = Soft;
	R.Space[0] = Space;
	R.Room = Room;
	R.Echo = Echo;
	R.Repeats = Repeats;
	return R;
}

Run Tone(int Kind, int Soft = 0, int Space = 0, int Room = 1, double Echo = 0.0, int Repeats = 3)
{
	Run R = Deep(Kind == TONE_TRIANGLE ? CHIP_2A03 : CHIP_VRC6, 1, Soft, Space, Room, Echo, Repeats);
	if (Kind == TONE_TRIANGLE) {
		R.Soft[0] = R.Space[0] = 0;
		R.Soft[2] = Soft;
		R.Space[2] = Space;
	}
	R.Tone = Kind;
	return R;
}

struct Sides {
	std::vector<int16> Left, Right;
};

// FamiTracker's sound output: the APU's samples, in turn for each side when in stereo
class Output : public IAudioCallback {
public:
	explicit Output(bool Stereo) : m_bStereo(Stereo) {}
	void FlushBuffer(int16 *pBuffer, uint32 Size)
	{
		for (uint32 i = 0; i < Size; ++i) {
			m_Sides.Left.push_back(m_bStereo ? pBuffer[i * 2] : pBuffer[i]);
			m_Sides.Right.push_back(m_bStereo ? pBuffer[i * 2 + 1] : pBuffer[i]);
		}
	}
	Sides m_Sides;
private:
	bool m_bStereo;
};

// Writes registers as CSoundGen::UpdateAPU does: a channel at a time, with some time between them
class Writer {
public:
	explicit Writer(CAPU &Apu) : m_Apu(Apu), m_iUsed(0) {}
	void W(uint16 Address, uint8 Value) { m_Apu.Write(Address, Value); }
	void X(uint16 Address, uint8 Value) { m_Apu.ExternalWrite(Address, Value); }
	void Next() { m_Apu.AddTime(250); m_Apu.Process(); m_iUsed += 250; }
	uint32 Used() const { return m_iUsed; }
private:
	CAPU &m_Apu;
	uint32 m_iUsed;
};

// The scripts: a few notes on each channel, with changes of volume and pitch along the way
void Play2A03(Writer &w, unsigned Mask, int f)
{
	if (Mask & 1) {
		if (f == 0) { w.W(0x4000, 0xBF); w.W(0x4001, 0x08); w.W(0x4002, 0xFE); w.W(0x4003, 0x00); }
		if (f == 10) w.W(0x4000, 0xB8);
		if (f == 20) w.W(0x4002, 0x7F);
		if (f == 30) w.W(0x4000, 0xB0);
	}
	w.Next();
	if (Mask & 2) {
		if (f == 0) { w.W(0x4004, 0x7A); w.W(0x4005, 0x08); w.W(0x4006, 0x7C); w.W(0x4007, 0x01); }
		if (f == 15) w.W(0x4004, 0x7F);
		if (f == 25) w.W(0x4006, 0x3E);
		if (f == 33) w.W(0x4004, 0x70);
	}
	w.Next();
	if (Mask & 4) {
		if (f == 0) { w.W(0x4008, 0xFF); w.W(0x400A, 0x00); w.W(0x400B, 0x02); }
		if (f == 18) w.W(0x400A, 0x80);
	}
	w.Next();
	if (Mask & 8) {
		if (f == 0) { w.W(0x400C, 0x3C); w.W(0x400E, 0x04); w.W(0x400F, 0x00); }
		if (f == 22) w.W(0x400E, 0x8A);
		if (f == 34) w.W(0x400C, 0x30);
	}
	w.Next();
	if (Mask & 16) {
		if (f == 0) { w.W(0x4010, 0x0E); w.W(0x4011, 0x40); w.W(0x4012, 0x00); w.W(0x4013, 0x20); w.W(0x4015, 0x1F); }
		if (f == 20) { w.W(0x4015, 0x0F); w.W(0x4015, 0x1F); }
	}
	w.Next();
}

void PlayVRC6(Writer &w, unsigned Mask, int f)
{
	if (Mask & 1) {
		if (f == 0) { w.X(0x9000, 0x3F); w.X(0x9001, 0x80); w.X(0x9002, 0x81); }
		if (f == 12) w.X(0x9000, 0x37);
		if (f == 28) w.X(0x9001, 0x40);
		if (f == 34) w.X(0x9002, 0x01);
	}
	w.Next();
	if (Mask & 2) {
		if (f == 0) { w.X(0xA000, 0x5A); w.X(0xA001, 0x20); w.X(0xA002, 0x82); }
		if (f == 24) w.X(0xA001, 0xC0);
	}
	w.Next();
	if (Mask & 4) {
		if (f == 0) { w.X(0xB000, 0x20); w.X(0xB001, 0x00); w.X(0xB002, 0x83); }
		if (f == 30) w.X(0xB000, 0x10);
	}
	w.Next();
}

void PlayMMC5(Writer &w, unsigned Mask, int f)
{
	if (Mask & 1) {
		if (f == 0) { w.X(0x5000, 0xBF); w.X(0x5002, 0xFE); w.X(0x5003, 0x00); }
		if (f == 14) w.X(0x5000, 0xB6);
		if (f == 26) w.X(0x5002, 0x50);
	}
	w.Next();
	if (Mask & 2) {
		if (f == 0) { w.X(0x5004, 0x7A); w.X(0x5006, 0x7C); w.X(0x5007, 0x01); }
		if (f == 20) w.X(0x5006, 0x3E);
	}
	w.Next();
}

void PlayFDS(Writer &w, unsigned Mask, int f)
{
	if (Mask & 1) {
		if (f == 0) {
			w.X(0x4089, 0x80);
			for (int i = 0; i < 64; ++i)
				w.X(static_cast<uint16>(0x4040 + i), static_cast<uint8>(i < 32 ? i * 2 : (63 - i) * 2));
			w.X(0x4089, 0x00);
			w.X(0x4080, 0xA0);
			w.X(0x4082, 0x40);
			w.X(0x4083, 0x02);
			w.X(0x4087, 0x80);
		}
		if (f == 16) w.X(0x4080, 0x90);
		if (f == 26) w.X(0x4082, 0x90);
		if (f == 34) w.X(0x4080, 0x80);
	}
	w.Next();
}

// Four channels play in turn on the one DAC, whichever of them sound
void PlayN163(Writer &w, unsigned Mask, int f)
{
	if (f == 0) {
		w.X(0xF800, 0x80);	// a wave of 32 samples at 0, a triangle
		for (int j = 0; j < 16; ++j) {
			const int a = 2 * j < 16 ? 2 * j : 31 - 2 * j, b = 2 * j + 1 < 16 ? 2 * j + 1 : 30 - 2 * j;
			w.X(0x4800, static_cast<uint8>(a | (b << 4)));
		}
		for (int k = 0; k < 4; ++k) {
			const int Freq = 0x08000 + k * 0x02800;
			const int Volume = (Mask & (1u << k)) ? 15 - 2 * k : 0;
			w.X(0xF800, static_cast<uint8>(0x80 | (0x78 - 8 * k)));
			w.X(0x4800, static_cast<uint8>(Freq & 0xFF));
			w.X(0x4800, 0x00);
			w.X(0x4800, static_cast<uint8>((Freq >> 8) & 0xFF));
			w.X(0x4800, 0x00);
			w.X(0x4800, static_cast<uint8>(0xE0 | ((Freq >> 16) & 0x03)));
			w.X(0x4800, 0x00);
			w.X(0x4800, 0x00);
			w.X(0x4800, static_cast<uint8>((k == 0 ? 0x30 : 0x00) | Volume));
			w.Next();
		}
	}
	if (f == 20) {
		w.X(0xF800, 0x7F);
		w.X(0x4800, static_cast<uint8>(0x30 | ((Mask & 1) ? 8 : 0)));
	}
	if (f == 30) {
		w.X(0xF800, 0x70);
		w.X(0x4800, 0x40);
	}
	w.Next();
}

void PlayVRC7(Writer &w, unsigned Mask, int f)
{
	for (int k = 0; k < 3; ++k) {
		if (Mask & (1u << k)) {
			const int Patch = 1 + 2 * k, Fnum = 0x0AC + 0x30 * k, Octave = 4;
			if (f == 0) {
				w.X(0x9010, static_cast<uint8>(0x30 + k));
				w.X(0x9030, static_cast<uint8>(Patch << 4));
				w.X(0x9010, static_cast<uint8>(0x10 + k));
				w.X(0x9030, static_cast<uint8>(Fnum & 0xFF));
				w.X(0x9010, static_cast<uint8>(0x20 + k));
				w.X(0x9030, static_cast<uint8>(0x10 | (Octave << 1) | (Fnum >> 8)));
			}
			if (f == 20 && k == 0) {
				w.X(0x9010, 0x20);
				w.X(0x9030, static_cast<uint8>((Octave << 1) | (Fnum >> 8)));
			}
			if (f == 28 && k == 1) {
				w.X(0x9010, 0x31);
				w.X(0x9030, static_cast<uint8>((Patch << 4) | 6));
			}
		}
		w.Next();
	}
}

void PlayS5B(Writer &w, unsigned Mask, int f)
{
	if (f == 0) {
		w.X(0xC000, 7);
		w.X(0xE000, static_cast<uint8>(0x38 | (~Mask & 7)));
		const int PERIODS[3] = { 0x0FE, 0x17C, 0x140 };
		for (int k = 0; k < 3; ++k) {
			w.X(0xC000, static_cast<uint8>(2 * k));
			w.X(0xE000, static_cast<uint8>(PERIODS[k] & 0xFF));
			w.X(0xC000, static_cast<uint8>(2 * k + 1));
			w.X(0xE000, static_cast<uint8>(PERIODS[k] >> 8));
			w.X(0xC000, static_cast<uint8>(8 + k));
			w.X(0xE000, static_cast<uint8>((Mask & (1u << k)) ? 15 - 2 * k : 0));
			w.Next();
		}
	}
	if (f == 20) {
		w.X(0xC000, 8);
		w.X(0xE000, static_cast<uint8>((Mask & 1) ? 8 : 0));
	}
	if (f == 30) {
		w.X(0xC000, 2);
		w.X(0xE000, 0x3E);
	}
	w.Next();
}

void PlayTone(Writer &w, int Tone, int f)
{
	// Period 383 on the VRC6 (16 cycles a step) and 191 on the triangle (32 a step): both 291.3 Hz
	if (Tone == TONE_TRIANGLE) {
		if (f == 0) { w.W(0x4008, 0xFF); w.W(0x400A, 0xBF); w.W(0x400B, 0x00); }
	}
	else {
		if (f == 0) { w.X(0x9000, 0x7F); w.X(0x9001, 0x7F); w.X(0x9002, 0x81); }
		if (Tone == TONE_BLIP && f == 10) w.X(0x9002, 0x01);
	}
	w.Next();
}

// Samples for the DPCM channel
char g_Samples[0x4000];

Sides RenderHere(const Run &R)
{
#ifdef APU_REFERENCE
	const bool Stereo = false;
#else
	const bool Stereo = true;
#endif
	uint32 Seed = 12345;
	for (size_t i = 0; i < sizeof(g_Samples); ++i) {
		Seed = Seed * 1103515245u + 12345u;
		g_Samples[i] = static_cast<char>(Seed >> 24);
	}
	Output Out(Stereo);
	CSampleMem Memory;
	Memory.SetMem(g_Samples, sizeof(g_Samples));
	CAPU Apu(&Out, &Memory);
	Apu.SetupSound(SAMPLE_RATE, Stereo ? 2 : 1, MACHINE_NTSC);
	Apu.SetupMixer(30, 12000, 24, 60);
	Apu.SetExternalSound(CHIP_FLAGS[R.Chip]);
	Apu.Reset();
	Apu.Write(0x4015, 0x0F);
	Apu.Write(0x4017, 0x00);
	if (R.Chip == CHIP_MMC5)
		Apu.ExternalWrite(0x5015, 0x03);

	const uint32 Cycles = CAPU::BASE_FREQ_NTSC / CAPU::FRAME_RATE_NTSC;
	const int Frames = R.Frames > 0 ? R.Frames : FRAMES;
	for (int f = 0; f < Frames; ++f) {
		Writer w(Apu);
#ifndef APU_REFERENCE
		if (R.Depth && f == R.DepthFrame) {
			Apu.SetSpace(R.Room, R.Echo, R.Repeats);
			for (int v = 0; v < VOICES; ++v)
				if (CHANNELS_OF[R.Chip][v] >= 0)
					Apu.SetChannelDepth(CHANNELS_OF[R.Chip][v], R.Soft[v], R.Space[v]);
		}
		if (R.Off > 0 && f == R.Off - 1)
			for (int v = 0; v < VOICES; ++v)
				if (CHANNELS_OF[R.Chip][v] >= 0)
					Apu.SetChannelDepth(CHANNELS_OF[R.Chip][v], 0, 0);
		if (f == R.PanFrame) {
			for (int v = 0; v < VOICES; ++v) {
				if (CHANNELS_OF[R.Chip][v] >= 0 && R.Pan[v] != 0x80)
					Apu.SetChannelPan(CHANNELS_OF[R.Chip][v], R.Pan[v]);
				if (CHANNELS_OF[R.Chip][v] >= 0 && R.Gain[v] != 1.0)
					Apu.SetChannelGain(CHANNELS_OF[R.Chip][v], R.Gain[v]);
			}
		}
		if (R.Tone != TONE_NONE) {
			PlayTone(w, R.Tone, f);
			Apu.AddTime(static_cast<int32>(Cycles - w.Used()));
			Apu.Process();
			continue;
		}
#endif
		switch (R.Chip) {
		case CHIP_2A03: Play2A03(w, R.Mask, f); break;
		case CHIP_VRC6: Play2A03(w, 0, f); PlayVRC6(w, R.Mask, f); break;
		case CHIP_MMC5: Play2A03(w, 0, f); PlayMMC5(w, R.Mask, f); break;
		case CHIP_FDS: Play2A03(w, 0, f); PlayFDS(w, R.Mask, f); break;
		case CHIP_N163: Play2A03(w, 0, f); PlayN163(w, R.Mask, f); break;
		case CHIP_VRC7: Play2A03(w, 0, f); PlayVRC7(w, R.Mask, f); break;
		case CHIP_S5B: Play2A03(w, 0, f); PlayS5B(w, R.Mask, f); break;
		}
		Apu.AddTime(static_cast<int32>(Cycles - w.Used()));
		Apu.Process();
	}
	return Out.m_Sides;
}

bool WriteAll(int File, const void *pData, size_t Size)
{
	const char *p = static_cast<const char*>(pData);
	while (Size > 0) {
		const ssize_t Done = write(File, p, Size);
		if (Done <= 0)
			return false;
		p += Done;
		Size -= static_cast<size_t>(Done);
	}
	return true;
}

bool ReadAll(int File, void *pData, size_t Size)
{
	char *p = static_cast<char*>(pData);
	while (Size > 0) {
		const ssize_t Done = read(File, p, Size);
		if (Done <= 0)
			return false;
		p += Done;
		Size -= static_cast<size_t>(Done);
	}
	return true;
}

// A rendering, in a process of its own
Sides Render(const Run &R)
{
	int Pipe[2];
	if (pipe(Pipe) != 0) {
		std::perror("pipe");
		std::exit(2);
	}
	const pid_t Child = fork();
	if (Child < 0) {
		std::perror("fork");
		std::exit(2);
	}
	if (Child == 0) {
		close(Pipe[0]);
		const Sides Made = RenderHere(R);
		const uint32_t Count = static_cast<uint32_t>(Made.Left.size());
		const bool Sent = WriteAll(Pipe[1], &Count, sizeof(Count)) && WriteAll(Pipe[1], Made.Left.data(), Count * sizeof(int16)) &&
			WriteAll(Pipe[1], Made.Right.data(), Count * sizeof(int16));
		close(Pipe[1]);
		_exit(Sent ? 0 : 3);
	}
	close(Pipe[1]);
	Sides Got;
	uint32_t Count = 0;
	if (ReadAll(Pipe[0], &Count, sizeof(Count)) && Count < 10000000) {
		Got.Left.resize(Count);
		Got.Right.resize(Count);
		if (!ReadAll(Pipe[0], Got.Left.data(), Count * sizeof(int16)) || !ReadAll(Pipe[0], Got.Right.data(), Count * sizeof(int16)))
			Got = Sides();
	}
	close(Pipe[0]);
	int Status = 0;
	waitpid(Child, &Status, 0);
	if (!WIFEXITED(Status) || WEXITSTATUS(Status) != 0) {
		std::printf("FAIL: rendering %s (mask %X) stopped\n", CHIP_NAMES[R.Chip], R.Mask);
		++g_fail;
	}
	return Got;
}

uint64_t Hash(const std::vector<int16> &Samples)
{
	uint64_t h = 1469598103934665603ULL;
	for (size_t i = 0; i < Samples.size(); ++i) {
		const uint16_t s = static_cast<uint16_t>(Samples[i]);
		h = (h ^ (s & 0xFF)) * 1099511628211ULL;
		h = (h ^ (s >> 8)) * 1099511628211ULL;
	}
	return h;
}

int Peak(const std::vector<int16> &Samples, size_t From = 0, size_t To = static_cast<size_t>(-1))
{
	int Most = 0;
	for (size_t i = From; i < Samples.size() && i < To; ++i)
		Most = std::max(Most, std::abs(static_cast<int>(Samples[i])));
	return Most;
}

#ifndef APU_REFERENCE

double Rms(const std::vector<int16> &Samples)
{
	double Sum = 0.0;
	for (size_t i = 0; i < Samples.size(); ++i)
		Sum += double(Samples[i]) * Samples[i];
	return Samples.empty() ? 0.0 : std::sqrt(Sum / Samples.size());
}

bool Same(const std::vector<int16> &First, const std::vector<int16> &Second, size_t Count = static_cast<size_t>(-1))
{
	if (Count == static_cast<size_t>(-1)) {
		if (First.size() != Second.size())
			return false;
		Count = First.size();
	}
	if (First.size() < Count || Second.size() < Count)
		return false;
	for (size_t i = 0; i < Count; ++i) {
		if (First[i] != Second[i])
			return false;
	}
	return true;
}

// The other side of a channel panned halfway: half of it, give or take the rounding of its level
// to whole steps (a pulse at volume 15 has 7 or 8 on it)
bool Half(const std::vector<int16> &Whole, const std::vector<int16> &Part)
{
	if (Whole.size() != Part.size() || Rms(Whole) <= 0.0)
		return false;
	const double Ratio = Rms(Part) / Rms(Whole);
	if (Ratio < 0.45 || Ratio > 0.56)
		return false;
	const int Allowed = Peak(Whole) / 8 + 16;
	for (size_t i = 0; i < Whole.size(); ++i) {
		if (std::abs(2 * Part[i] - Whole[i]) > Allowed)
			return false;
	}
	return true;
}

void TestMiddle()
{
	// Every channel in the middle: both sides are the mono mix
	for (int Chip = 0; Chip < CHIPS; ++Chip) {
		const Sides Made = Render(Plain(Chip, ALL));
		CHECK(Made.Left.size() >= static_cast<size_t>(FRAMES - 1) * FRAME_SAMPLES);
		CHECK(Peak(Made.Left) > 1000);
		CHECK(Same(Made.Left, Made.Right));
		if (Hash(Made.Left) != MONO[Chip]) {
			std::printf("FAIL: %s isn't the mono mix: 0x%016llxULL\n", CHIP_NAMES[Chip], static_cast<unsigned long long>(Hash(Made.Left)));
			++g_fail;
		}
		++g_checks;
	}
}

void TestHardPans()
{
	// Two channels to opposite sides: each side has one of them as it sounds alone in the middle
	for (int Chip = 0; Chip < CHIPS; ++Chip) {
		const Sides First = Render(Plain(Chip, 1));
		CHECK(Peak(First.Left) > 500);
		if (CHANNELS_OF[Chip][1] < 0) {
			// One channel: the other side is silent
			const Sides Left = Render(Panned(Chip, 1, 0x00)), Right = Render(Panned(Chip, 1, 0xFF));
			CHECK(Same(Left.Left, First.Left) && Peak(Left.Right) == 0);
			CHECK(Same(Right.Right, First.Left) && Peak(Right.Left) == 0);
			continue;
		}
		const Sides Second = Render(Plain(Chip, 2));
		CHECK(Peak(Second.Left) > 500);
		const Sides Apart = Render(Panned(Chip, 3, 0x00, 0xFF));
		if (!Same(Apart.Left, First.Left) || !Same(Apart.Right, Second.Left)) {
			std::printf("FAIL: %s, left and right\n", CHIP_NAMES[Chip]);
			++g_fail;
		}
		++g_checks;
		const Sides Swapped = Render(Panned(Chip, 3, 0xFF, 0x00));
		if (!Same(Swapped.Left, Second.Left) || !Same(Swapped.Right, First.Left)) {
			std::printf("FAIL: %s, right and left\n", CHIP_NAMES[Chip]);
			++g_fail;
		}
		++g_checks;
	}
}

void TestHalfway()
{
	// Halfway left (0x40): all of it on the left, half on the right. The 2A03 mixes its channels
	// nonlinearly, so half its level sounds a little louder than half.
	for (int Chip = 0; Chip < CHIPS; ++Chip) {
		const Sides Middle = Render(Plain(Chip, 1));
		const Sides Moved = Render(Panned(Chip, 1, 0x40));
		CHECK(Same(Moved.Left, Middle.Left));
		if (Chip == CHIP_2A03) {
			const double Ratio = Rms(Moved.Right) / Rms(Middle.Left);
			CHECK(Ratio > 0.48 && Ratio < 0.6);
		}
		else if (!Half(Middle.Left, Moved.Right)) {
			std::printf("FAIL: %s, halfway left: %.3f of it on the right\n", CHIP_NAMES[Chip], Rms(Moved.Right) / Rms(Middle.Left));
			++g_fail;
		}
		++g_checks;
		// ...and halfway right the other way round (0xC0, a little less than half way: 63/127)
		const Sides Other = Render(Panned(Chip, 1, 0xC0));
		CHECK(Same(Other.Right, Middle.Left));
		const double Ratio = Rms(Other.Left) / Rms(Middle.Left);
		CHECK(Ratio > 0.45 && Ratio < 0.6);
	}
}

void TestMoving()
{
	// Panned left while it sounds: until then both sides are as in the middle, after it the right
	// side fades out of it (the low cut lets the step go) and has nothing
	for (int Chip = 0; Chip < CHIPS; ++Chip) {
		const Sides Middle = Render(Plain(Chip, 1));
		const Sides Moved = Render(Panned(Chip, 1, 0x00, 0x80, 12));
		const size_t Before = 11 * FRAME_SAMPLES;
		CHECK(Same(Moved.Left, Middle.Left));
		CHECK(Same(Moved.Right, Middle.Left, Before));
		if (Peak(Moved.Right, 20 * FRAME_SAMPLES) > 2) {
			std::printf("FAIL: %s, still on the right after it was panned left: %d\n", CHIP_NAMES[Chip], Peak(Moved.Right, 20 * FRAME_SAMPLES));
			++g_fail;
		}
		++g_checks;
	}
}

void TestLevels()
{
	// The mixer window's level: a channel at a gain of one half is half as loud, on both sides
	for (int Chip = 0; Chip < CHIPS; ++Chip) {
		const Sides Middle = Render(Plain(Chip, 1));
		const Sides Halved = Render(Leveled(Chip, 1, 0.5));
		const double Half_ = Rms(Halved.Left) / Rms(Middle.Left);
		// Exactly half on the VRC6, MMC5, FDS and VRC7 (the VRC6's and MMC5's small levels play through a synth of
		// the channel's own, so they aren't rounded); the 2A03's pins and the 5B's and N163's levels go in whole steps
		const bool Exact = Chip == CHIP_VRC6 || Chip == CHIP_MMC5 || Chip == CHIP_FDS || Chip == CHIP_VRC7;
		if (!Half(Middle.Left, Halved.Left) || !Same(Halved.Left, Halved.Right) || (Exact && std::fabs(Half_ - 0.5) > 0.006)) {
			std::printf("FAIL: %s at half its level: %.4f of it\n", CHIP_NAMES[Chip], Half_);
			++g_fail;
		}
		++g_checks;
		// Twice as loud (the VRC7 clips its own output, as the chip does, so it may come out a little under)
		const Sides Doubled = Render(Leveled(Chip, 1, 2.0));
		const double Ratio = Rms(Doubled.Left) / Rms(Middle.Left);
		if (Ratio < (Chip == CHIP_VRC7 ? 1.5 : 1.9) || Ratio > 2.1) {
			std::printf("FAIL: %s at twice its level: %.3f of it\n", CHIP_NAMES[Chip], Ratio);
			++g_fail;
		}
		++g_checks;
		// One silent: as if it didn't play, on the chips that mix their channels apart (the 2A03's pins, the N163's
		// one DAC and the VRC7 mix theirs together)
		if (CHANNELS_OF[Chip][1] >= 0 && Chip != CHIP_2A03 && Chip != CHIP_VRC7 && Chip != CHIP_N163) {
			const Sides Second = Render(Plain(Chip, 2));
			const Sides Without = Render(Leveled(Chip, 3, 0.0));
			if (!Same(Without.Left, Second.Left)) {
				std::printf("FAIL: %s, its first channel silenced isn't the second alone\n", CHIP_NAMES[Chip]);
				++g_fail;
			}
			++g_checks;
		}
	}
	// The 2A03's pulses share a pin: one at half, the other as it was (its share of the pin kept)
	const Sides Both = Render(Plain(CHIP_2A03, 3)), Halved = Render(Leveled(CHIP_2A03, 3, 0.5)), Second = Render(Plain(CHIP_2A03, 2));
	const double Ratio = Rms(Halved.Left) / Rms(Both.Left);
	CHECK(Ratio > 0.6 && Ratio < 0.95 && Rms(Halved.Left) > Rms(Second.Left) * 0.9);
}

// Depth

const int ROOM_NONE = 4;	// CSpace::ROOM_NONE: the echo alone

double RmsOf(const std::vector<int16> &Samples, size_t From, size_t To)
{
	double Sum = 0.0;
	size_t n = 0;
	for (size_t i = From; i < To && i < Samples.size(); ++i, ++n)
		Sum += double(Samples[i]) * Samples[i];
	return n == 0 ? 0.0 : std::sqrt(Sum / n);
}

int MostApart(const std::vector<int16> &First, const std::vector<int16> &Second)
{
	int Most = First.size() == Second.size() ? 0 : 1 << 20;
	for (size_t i = 0; i < First.size() && i < Second.size(); ++i)
		Most = std::max(Most, std::abs(int(First[i]) - int(Second[i])));
	return Most;
}

// The energy of samples From to To over 2 kHz (two Butterworth high-passes) and in all
void Energies(const std::vector<int16> &Samples, size_t From, size_t To, double &High, double &All)
{
	const double Pi = 3.14159265358979323846, w = 2.0 * Pi * 2000.0 / SAMPLE_RATE, Alpha = std::sin(w) / (2.0 * 0.70710678118654752);
	const double a0 = 1.0 + Alpha, b0 = (1.0 + std::cos(w)) / 2.0 / a0, b1 = -(1.0 + std::cos(w)) / a0, a1 = -2.0 * std::cos(w) / a0,
		a2 = (1.0 - Alpha) / a0;
	double x1[2] = { 0, 0 }, x2[2] = { 0, 0 }, y1[2] = { 0, 0 }, y2[2] = { 0, 0 };
	High = All = 0.0;
	for (size_t i = 0; i < To && i < Samples.size(); ++i) {
		double x = Samples[i];
		if (i >= From)
			All += x * x;
		for (int k = 0; k < 2; ++k) {
			const double y = b0 * x + b1 * x1[k] + b0 * x2[k] - a1 * y1[k] - a2 * y2[k];
			x2[k] = x1[k];
			x1[k] = x;
			y2[k] = y1[k];
			y1[k] = y;
			x = y;
		}
		if (i >= From)
			High += x * x;
	}
}

double Db(double Ratio)
{
	return Ratio > 0.0 ? 10.0 * std::log10(Ratio) : -300.0;
}

void TestDepthOff()
{
	for (int Chip = 0; Chip < CHIPS; ++Chip) {
		// Soft and Space at 0, said so: the mono mix
		const Sides Zero = Render(Deep(Chip, ALL, 0, 0));
		if (Hash(Zero.Left) != MONO[Chip] || !Same(Zero.Left, Zero.Right)) {
			std::printf("FAIL: %s with Soft and Space at 0 isn't the mono mix\n", CHIP_NAMES[Chip]);
			++g_fail;
		}
		++g_checks;
		// Every channel in its own buffers before anything sounds, and back at once: the same (its buffers
		// are read for a second after, and have nothing)
		Run Back = Deep(Chip, ALL, 60, 40, 2, 0.1, 4);
		for (int v = 0; v < VOICES; ++v) {
			Back.Soft[v] = 60;
			Back.Space[v] = 40;
		}
		Back.Off = 1;
		const Sides Again = Render(Back);
		if (Hash(Again.Left) != MONO[Chip] || !Same(Again.Left, Again.Right)) {
			std::printf("FAIL: %s with Soft and Space set and cleared isn't the mono mix\n", CHIP_NAMES[Chip]);
			++g_fail;
		}
		++g_checks;
	}
}

void TestOwnBuffers()
{
	// A channel in its own buffers with nothing done there (Space without a room or an echo) sounds as it did,
	// from the start and moved there while it sounds, on every chip: the sides and its own buffers add up
	// (each rounded apart, and the 2A03's pins' steps to whole units of the synth)
	// (the VRC7 clips the sum of its channels, as the chip does, and a channel in its own buffers is clipped apart, so
	// there the channel plays alone)
	const int ALLOWED[CHIPS] = { 2, 2, 2, 2, 2, 2, 2 };
	for (int Chip = 0; Chip < CHIPS; ++Chip) {
		const unsigned Mask = Chip == CHIP_VRC7 ? 1 : ALL;
		const Sides Dry = Render(Plain(Chip, Mask));
		const Sides Through = Render(Deep(Chip, Mask, 0, 50, ROOM_NONE));
		Run Later = Deep(Chip, Mask, 0, 50, ROOM_NONE);
		Later.DepthFrame = 12;
		Later.Off = 31;
		const Sides Moved = Render(Later);
		const int Apart = MostApart(Dry.Left, Through.Left), Apart2 = MostApart(Dry.Left, Moved.Left);
		if (Apart > ALLOWED[Chip] || Apart2 > ALLOWED[Chip] || !Same(Through.Left, Through.Right) || !Same(Moved.Left, Moved.Right)) {
			std::printf("FAIL: %s through its own buffers: %d and %d apart from the mix\n", CHIP_NAMES[Chip], Apart, Apart2);
			++g_fail;
		}
		++g_checks;
	}
}

void TestSoft()
{
	// The pulse softened: the share of its sound over 2 kHz taken off, as designed (about 2.5 dB at 50, 7 at 100),
	// as loud as before, and brighter than a triangle at the same pitch
	const size_t From = 5 * FRAME_SAMPLES, To = 39 * FRAME_SAMPLES;
	double High[3], All[3], TriHigh, TriAll;
	const int SOFTS[3] = { 0, 50, 100 };
	for (int k = 0; k < 3; ++k) {
		const Sides Made = Render(Tone(TONE_PULSE, SOFTS[k]));
		Energies(Made.Left, From, To, High[k], All[k]);
		CHECK(Same(Made.Left, Made.Right));
	}
	const Sides Tri = Render(Tone(TONE_TRIANGLE));
	Energies(Tri.Left, From, To, TriHigh, TriAll);
	const double Half = Db(High[0] / High[1]), Full = Db(High[0] / High[2]), Level = Db(All[2] / All[0]);
	std::printf("Soft on a 291 Hz pulse: %.1f dB over 2 kHz off at 50, %.1f at 100, its level %+.2f dB; over 2 kHz: %.2f%% of it "
		"at 100, %.2f%% of a triangle's\n", Half, Full, Level, 100.0 * High[2] / All[2], 100.0 * TriHigh / TriAll);
	CHECK(Half > 1.8 && Half < 4.0);
	CHECK(Full > 5.5 && Full < 9.0);
	CHECK(std::fabs(Level) < 1.0);
	CHECK(High[2] / All[2] > 1.5 * TriHigh / TriAll);
	// The triangle softened too (Soft goes on any channel)
	const Sides SoftTri = Render(Tone(TONE_TRIANGLE, 100));
	double SoftTriHigh, SoftTriAll;
	Energies(SoftTri.Left, From, To, SoftTriHigh, SoftTriAll);
	CHECK(SoftTriHigh < TriHigh && SoftTriAll > 0.8 * TriAll);

	// Every chip: its first channel at Soft 100 has less over 2 kHz and about the level it had
	for (int Chip = 0; Chip < CHIPS; ++Chip) {
		const Sides Dry = Render(Plain(Chip, 1)), Soft = Render(Deep(Chip, 1, 100, 0));
		double DryHigh, DryAll, SoftHigh, SoftAll;
		Energies(Dry.Left, 0, Dry.Left.size(), DryHigh, DryAll);
		Energies(Soft.Left, 0, Soft.Left.size(), SoftHigh, SoftAll);
		if (!(Db(DryHigh / SoftHigh) > 2.0) || std::fabs(Db(SoftAll / DryAll)) > 1.5 || !Same(Soft.Left, Soft.Right)) {
			std::printf("FAIL: %s at Soft 100: %.1f dB over 2 kHz off, its level %+.2f dB\n", CHIP_NAMES[Chip], Db(DryHigh / SoftHigh),
				Db(SoftAll / DryAll));
			++g_fail;
		}
		++g_checks;
	}
}

void TestSpace()
{
	// A short note (10 frames) with all of it sent: after it, the room rings on, dying away, the same every time,
	// unlike on the two sides; the note without Space is silent there
	Run DryRun = Tone(TONE_BLIP);
	DryRun.Frames = 80;
	const Sides Dry = Render(DryRun);
	const double Note = RmsOf(Dry.Left, 2 * FRAME_SAMPLES, 9 * FRAME_SAMPLES);
	CHECK(Note > 1000.0 && Peak(Dry.Left, 14 * FRAME_SAMPLES) < 20);
	const char *const ROOMS[4] = { "small room", "hall", "cathedral", "cave" };
	for (int Room = 0; Room < 4; ++Room) {
		Run R = Tone(TONE_BLIP, 0, 100, Room);
		R.Frames = 80;
		const Sides A = Render(R), B = Render(R);
		const double Early = RmsOf(A.Left, 12 * FRAME_SAMPLES, 22 * FRAME_SAMPLES), Late = RmsOf(A.Left, 60 * FRAME_SAMPLES, 80 * FRAME_SAMPLES);
		std::printf("Space 100 in a %s: %.1f dB after the note (frames 12-22), %.1f dB a second later\n", ROOMS[Room], 20.0 * std::log10(Early / Note),
			Late > 0.0 ? 20.0 * std::log10(Late / Note) : -300.0);
		if (!Same(A.Left, B.Left) || !Same(A.Right, B.Right) || Same(A.Left, A.Right) || !(Early > Note * 0.02) || !(Late < Early)) {
			std::printf("FAIL: Space in a %s\n", ROOMS[Room]);
			++g_fail;
		}
		++g_checks;
		// While the note sounds, it's there as it was (with the room's start added)
		CHECK(RmsOf(A.Left, 2 * FRAME_SAMPLES, 9 * FRAME_SAMPLES) > Note * 0.9);
	}
	// The echo alone, a quarter of a second, 3 repeats: silent until the first, each repeat about 10 dB under the one
	// before (and a little darker), the first at 0.6 of the note
	Run E = Tone(TONE_BLIP, 0, 100, ROOM_NONE, 0.25, 3);
	E.Frames = 70;
	const Sides Echo = Render(E);
	const double Gap = RmsOf(Echo.Left, 12 * FRAME_SAMPLES, 14 * FRAME_SAMPLES), First = RmsOf(Echo.Left, 17 * FRAME_SAMPLES, 24 * FRAME_SAMPLES),
		Second = RmsOf(Echo.Left, 32 * FRAME_SAMPLES, 39 * FRAME_SAMPLES), Third = RmsOf(Echo.Left, 47 * FRAME_SAMPLES, 54 * FRAME_SAMPLES),
		After = RmsOf(Echo.Left, 62 * FRAME_SAMPLES, 69 * FRAME_SAMPLES);
	std::printf("Echo of 0.25 s, 3 repeats: %.2f, %.2f and %.2f of the note, then %.3f\n", First / Note, Second / Note, Third / Note, After / Note);
	CHECK(Gap < First * 0.05);
	CHECK(First / Note > 0.45 && First / Note < 0.65);
	CHECK(Second / First > 0.22 && Second / First < 0.36);
	CHECK(Third / Second > 0.22 && Third / Second < 0.36);
	CHECK(After < Third * 0.5);
	CHECK(Same(Echo.Left, Echo.Right));
	// Post-fader: the channel at half its level sends half
	Run Halved = E;
	Halved.Gain[0] = 0.5;
	const Sides Less = Render(Halved);
	const double Ratio = RmsOf(Less.Left, 17 * FRAME_SAMPLES, 24 * FRAME_SAMPLES) / First;
	CHECK(Ratio > 0.45 && Ratio < 0.55);
}

#endif

}	// namespace

int main()
{
#ifdef APU_REFERENCE
	// The mono mix's hashes, for MONO
	for (int Chip = 0; Chip < CHIPS; ++Chip) {
		const Sides Made = Render(Plain(Chip, ALL));
		std::printf("\t0x%016llxULL,\t// %s (%u samples, peak %d)\n", static_cast<unsigned long long>(Hash(Made.Left)), CHIP_NAMES[Chip],
			static_cast<unsigned>(Made.Left.size()), Peak(Made.Left));
	}
	return g_fail == 0 ? 0 : 1;
#else
	TestMiddle();
	TestHardPans();
	TestHalfway();
	TestMoving();
	TestLevels();
	TestDepthOff();
	TestOwnBuffers();
	TestSoft();
	TestSpace();
	std::printf("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail == 0 ? 0 : 1;
#endif
}

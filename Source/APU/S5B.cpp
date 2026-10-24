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

#include <algorithm>
#include <cstdio>
#include "APU.h"
#include "S5B.h"
#include "emu2149.h"

// Sunsoft 5B (YM2149)
//
// emu2149 runs a tick at a time, every 16 CPU cycles: the chip's square
// toggles every 16 cycles times its period, its noise steps every 32 times its
// period and its envelope steps every 16 times its period (NESdev wiki). Each
// channel's level goes to the mixer as it changes, at the cycle it changed,
// through a band-limited synth like the other chips'.

static PSG *psg = NULL;

namespace {
const uint32 CYCLES_PER_TICK = 16;
}

CS5B::CS5B(CMixer *pMixer) : m_iRegister(0), m_iTime(0), m_iTickCycles(CYCLES_PER_TICK)
{
	m_pMixer = pMixer;
	for (int i = 0; i < 3; ++i)
		m_iLevels[i] = 0;

	// One tick a call: a clock 16 times the "sample rate"
	psg = PSG_new(CYCLES_PER_TICK, 1);
	PSG_setVolumeMode(psg, 1);		// the YM2149's 32 steps of 1.5 dB
	PSG_reset(psg);
}

CS5B::~CS5B()
{
	if (psg)
		PSG_delete(psg);
	psg = NULL;
}

void CS5B::Reset()
{
	// The mixer's buffer is cleared with the APU, so the levels start from silence
	m_iTime = 0;
	m_iTickCycles = CYCLES_PER_TICK;
	for (int i = 0; i < 3; ++i)
		m_iLevels[i] = 0;
	m_iRegister = 0;
	PSG_reset(psg);
}

void CS5B::Process(uint32 Time)
{
	while (Time > 0) {
		const uint32 Run = std::min(Time, m_iTickCycles);
		Time -= Run;
		m_iTime += Run;
		m_iTickCycles -= Run;
		if (m_iTickCycles == 0) {
			m_iTickCycles = CYCLES_PER_TICK;
			PSG_calc(psg);
			MixLevels();
		}
	}
}

void CS5B::MixLevels()
{
	// Each channel's output as emu2149 sums it: the tone and noise it lets
	// through, at its volume or the envelope's
	const bool Noise = (psg->noise_seed & 1) != 0;
	for (int i = 0; i < 3; ++i) {
		int32 Level = 0;
		if (!(psg->mask & PSG_MASK_CH(i)) && (psg->tmask[i] || psg->edge[i]) && (psg->nmask[i] || Noise))
			Level = static_cast<int32>(psg->voltbl[(psg->volume[i] & 32) ? (psg->env_ptr & 31) : (psg->volume[i] & 31)]);
		if (Level != m_iLevels[i]) {
			m_pMixer->AddValue(CHANID_S5B_CH1 + i, SNDCHIP_S5B, Level - m_iLevels[i], Level, m_iTime);
			m_iLevels[i] = Level;
		}
	}
}

void CS5B::EndFrame()
{
	m_iTime = 0;
}

void CS5B::Write(uint16 Address, uint8 Value)
{
	switch (Address) {
		case 0xC000:
			m_iRegister = Value & 0xF;
			break;
		case 0xE000:
			PSG_writeReg(psg, m_iRegister, Value);
			break;
	}
}

uint8 CS5B::Read(uint16 Address, bool &Mapped)
{
	// No reads here
	Mapped = false;
	return 0;
}

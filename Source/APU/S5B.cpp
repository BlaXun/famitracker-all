/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2014  Jonathan Liss
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

#include <cstdio>
#include "APU.h"
#include "S5B.h"
#include "emu2149.h"

// Sunsoft 5B (YM2149)

PSG *psg;

float CS5B::AMPLIFY = 2.0f;

CS5B::CS5B(CMixer *pMixer) : m_iRegister(0), m_iTime(0)
{
	m_pMixer = pMixer;

	m_fVolume = AMPLIFY;

	psg = NULL;
}

CS5B::~CS5B()
{
	if (psg)
		PSG_delete(psg);
}

void CS5B::Reset()
{
	m_iTime = 0;
//	PSG_reset(psg);
}

void CS5B::Process(uint32 Time)
{
	m_iTime += Time;
}

int16 m_pBuffer[4000];
int16 m_pBufferRight[4000];	// the right side (m_pBuffer is the left)
uint32 m_iBufferPtr = 0;

void CS5B::EndFrame()
{
	GetMixMono();
}

void CS5B::GetMixMono()
{
	static int32 LastSample = 0, LastRight = 0;

	uint32 WantSamples = m_pMixer->GetMixSampleCount(m_iTime);

	// The channels' pans, for the whole frame (stereo is this FamiTracker's own)
	int32 GainL[3], GainR[3];
	for (int i = 0; i < 3; ++i)
		m_pMixer->PanGains256(CHANID_S5B_CH1 + i, GainL[i], GainR[i]);

	// Generate samples, the left side and the right
	while (m_iBufferPtr < WantSamples) {
		int16 Left, Right;
		PSG_calc_lr(psg, GainL, GainR, &Left, &Right);
		int32 Sample = int32(float(Left) * m_fVolume);
		int32 SampleRight = int32(float(Right) * m_fVolume);
		m_pBufferRight[m_iBufferPtr] = int16((SampleRight + LastRight) >> 1);
		m_pBuffer[m_iBufferPtr++] = int16((Sample + LastSample) >> 1);
		LastSample = Sample;
		LastRight = SampleRight;
	}

	m_pMixer->MixSamples((blip_sample_t*)m_pBuffer, (blip_sample_t*)m_pBufferRight, WantSamples);

	m_iBufferPtr -= WantSamples;
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

void CS5B::SetSampleSpeed(uint32 SampleRate, double ClockRate, uint32 FrameRate)
{
	if (psg != NULL) {
		PSG_delete(psg);
	}

	//PSG_init((uint32)ClockRate, SampleRate);
	psg = PSG_new((uint32)ClockRate, SampleRate);
	PSG_setVolumeMode(psg, 1);
	PSG_reset(psg);

//	psg = PSG_new();

//	PSG_setVolumeMode(psg, 1);
//	PSG_reset(psg);
}

void CS5B::SetVolume(float fVol)
{
	m_fVolume = AMPLIFY * fVol;
}
/*
void CS5B::SetChannelVolume(int Chan, int LevelL, int LevelR)
{
	PSG_set_chan_vol(Chan, LevelL, LevelR);
}
*/
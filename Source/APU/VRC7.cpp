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

#include "../stdafx.h"
#include <memory>
#include "APU.h"
#include "VRC7.h"

const float  CVRC7::AMPLIFY	  = 4.6f;		// Mixing amplification, VRC7 patch 14 is 4,88 times stronger than a 50% square @ v=15
const uint32 CVRC7::OPL_CLOCK = 3579545;	// Clock frequency

CVRC7::CVRC7(CMixer *pMixer) : CExternal(pMixer), m_pBuffer(NULL), m_pBufferRight(NULL), m_pOPLLInt(NULL), m_fVolume(1.0f), m_iMaxSamples(0),
	m_iSoundReg(0)
{
	Reset();
}

CVRC7::~CVRC7()
{
	if (m_pOPLLInt != NULL) {
		OPLL_delete(m_pOPLLInt);
		m_pOPLLInt = NULL;
	}

	SAFE_RELEASE_ARRAY(m_pBuffer);
	SAFE_RELEASE_ARRAY(m_pBufferRight);
}

void CVRC7::Reset()
{
	m_iBufferPtr = 0;
	m_iTime = 0;
}

void CVRC7::SetSampleSpeed(uint32 SampleRate, double ClockRate, uint32 FrameRate)
{
	if (m_pOPLLInt != NULL) {
		OPLL_delete(m_pOPLLInt);
		m_pOPLLInt = NULL;
	}

	m_pOPLLInt = OPLL_new(OPL_CLOCK, SampleRate);

	OPLL_reset(m_pOPLLInt);
	OPLL_reset_patch(m_pOPLLInt, 1);

	m_iMaxSamples = (SampleRate / FrameRate) * 2;	// Allow some overflow

	SAFE_RELEASE_ARRAY(m_pBuffer);
	m_pBuffer = new int16[m_iMaxSamples];
	memset(m_pBuffer, 0, sizeof(int16) * m_iMaxSamples);
	SAFE_RELEASE_ARRAY(m_pBufferRight);
	m_pBufferRight = new int16[m_iMaxSamples];
	memset(m_pBufferRight, 0, sizeof(int16) * m_iMaxSamples);
}

void CVRC7::SetVolume(float Volume)
{
	m_fVolume = Volume * AMPLIFY;
}

void CVRC7::Write(uint16 Address, uint8 Value)
{
	switch (Address) {
		case 0x9010:
			m_iSoundReg = Value;
			break;
		case 0x9030:
			OPLL_writeReg(m_pOPLLInt, m_iSoundReg, Value);
			break;
	}
}

uint8 CVRC7::Read(uint16 Address, bool &Mapped)
{
	return 0;
}

void CVRC7::EndFrame()
{
	uint32 WantSamples = m_pMixer->GetMixSampleCount(m_iTime);

	static int32 LastSample[2] = { 0, 0 };

	// The channels' pans, for the whole frame (stereo is this FamiTracker's own)
	int32 GainL[6], GainR[6];
	for (int i = 0; i < 6; ++i)
		m_pMixer->PanGains256(CHANID_VRC7_CH1 + i, GainL[i], GainR[i]);

	// Generate VRC7 samples, the left side and the right
	while (m_iBufferPtr < WantSamples) {
		int16 Raw[2];
		OPLL_calc_lr(m_pOPLLInt, GainL, GainR, &Raw[0], &Raw[1]);

		for (int s = 0; s < 2; ++s) {
			int32 RawSample = Raw[s];

			// Clipping is slightly asymmetric
			if (RawSample > 3600)
				RawSample = 3600;
			if (RawSample < -3200)
				RawSample = -3200;

			// Apply volume
			int32 Sample = int(float(RawSample) * m_fVolume);

			if (Sample > 32767)
				Sample = 32767;
			if (Sample < -32768)
				Sample = -32768;

			(s == 0 ? m_pBuffer : m_pBufferRight)[m_iBufferPtr] = int16((Sample + LastSample[s]) >> 1);
			LastSample[s] = Sample;
		}
		++m_iBufferPtr;
	}

	m_pMixer->MixSamples((blip_sample_t*)m_pBuffer, (blip_sample_t*)m_pBufferRight, WantSamples);

	m_iBufferPtr -= WantSamples;
	m_iTime = 0;
}

void CVRC7::Process(uint32 Time)
{
	// This cannot run in sync, fetch all samples at end of frame instead
	m_iTime += Time;
}

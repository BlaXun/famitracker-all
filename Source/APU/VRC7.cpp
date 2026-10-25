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
#include <algorithm>
#include "APU.h"
#include "VRC7.h"

const float  CVRC7::AMPLIFY	  = 4.6f;		// Mixing amplification, VRC7 patch 14 is 4,88 times stronger than a 50% square @ v=15
const uint32 CVRC7::OPL_CLOCK = 3579545;	// Clock frequency

CVRC7::CVRC7(CMixer *pMixer) : CExternal(pMixer), m_pBuffer(NULL), m_pBufferRight(NULL), m_pOPLLInt(NULL), m_fVolume(1.0f), m_iMaxSamples(0),
	m_iSoundReg(0), m_iStems(0), m_pStemSamples(NULL), m_pStemLast(NULL)
{
	for (int i = 0; i < 6; ++i)
		for (int s = 0; s < 2; ++s) {
			m_pOwn[i][s] = NULL;
			m_iOwnLast[i][s] = 0;
		}
	m_iLastSample[0] = m_iLastSample[1] = 0;
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
	for (int i = 0; i < 6; ++i)
		for (int s = 0; s < 2; ++s)
			SAFE_RELEASE_ARRAY(m_pOwn[i][s]);
	FreeStems();
}

void CVRC7::FreeStems()
{
	SAFE_RELEASE_ARRAY(m_pStemSamples);
	SAFE_RELEASE_ARRAY(m_pStemLast);
	m_iStems = 0;
}

void CVRC7::SetStems(int Count)
{
	FreeStems();
	if (Count <= 0 || m_iMaxSamples == 0)
		return;
	m_pStemSamples = new int16[Count * STEM_BUFFERS * m_iMaxSamples];
	memset(m_pStemSamples, 0, sizeof(int16) * Count * STEM_BUFFERS * m_iMaxSamples);
	m_pStemLast = new int32[Count * STEM_BUFFERS];
	m_iStems = Count;
	// Each starts where the mix is, as a render of it alone would
	for (int k = 0; k < Count; ++k) {
		for (int s = 0; s < 2; ++s) {
			StemLast(k, s) = m_iLastSample[s];
			for (int i = 0; i < 6; ++i)
				StemLast(k, 2 + 2 * i + s) = m_iOwnLast[i][s];
		}
	}
}

int16 CVRC7::Finished(int32 RawSample, int32 &Last) const
{
	// As EndFrame finishes the mix's samples: clipping is slightly asymmetric
	if (RawSample > 3600)
		RawSample = 3600;
	if (RawSample < -3200)
		RawSample = -3200;
	int32 Sample = int(float(RawSample) * m_fVolume);
	if (Sample > 32767)
		Sample = 32767;
	if (Sample < -32768)
		Sample = -32768;
	const int16 Out = int16((Sample + Last) >> 1);
	Last = Sample;
	return Out;
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
	FreeStems();	// their buffers were as long as the frames before

	SAFE_RELEASE_ARRAY(m_pBuffer);
	m_pBuffer = new int16[m_iMaxSamples];
	memset(m_pBuffer, 0, sizeof(int16) * m_iMaxSamples);
	SAFE_RELEASE_ARRAY(m_pBufferRight);
	m_pBufferRight = new int16[m_iMaxSamples];
	memset(m_pBufferRight, 0, sizeof(int16) * m_iMaxSamples);
	for (int i = 0; i < 6; ++i)
		for (int s = 0; s < 2; ++s) {
			SAFE_RELEASE_ARRAY(m_pOwn[i][s]);
			m_pOwn[i][s] = new int16[m_iMaxSamples];
			memset(m_pOwn[i][s], 0, sizeof(int16) * m_iMaxSamples);
			m_iOwnLast[i][s] = 0;
		}
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

	int32 *const LastSample = m_iLastSample;

	// The channels' pans, for the whole frame (stereo is this FamiTracker's own)
	int32 GainL[6], GainR[6];
	for (int i = 0; i < 6; ++i)
		m_pMixer->PanGains256(CHANID_VRC7_CH1 + i, GainL[i], GainR[i]);
	// Depth (Soft or Space): the channels in buffers of their own get samples of their own, made as the chip's are
	bool Own[6], AnyOwn = false;
	int32 OwnGain[6][2];
	for (int i = 0; i < 6; ++i) {
		Own[i] = m_pMixer->IsOwn(CHANID_VRC7_CH1 + i) && m_pOwn[i][0] != NULL;
		OwnGain[i][0] = GainL[i];
		OwnGain[i][1] = GainR[i];
		if (Own[i]) {
			GainL[i] = GainR[i] = 0;
			AnyOwn = true;
		}
	}
	// Stems (CMixer::SetStems): each one's levels and pans, and its channels in buffers of their own, as its mixer has them
	const int Stems = std::min(m_iStems, m_pMixer->StemCount());
	int32 StemGain[CMixer::MAX_STEMS][6][2];
	bool StemOwn[CMixer::MAX_STEMS][6];
	for (int k = 0; k < Stems; ++k) {
		const CMixer *pStem = m_pMixer->Stem(k);
		for (int i = 0; i < 6; ++i) {
			pStem->PanGains256(CHANID_VRC7_CH1 + i, StemGain[k][i][0], StemGain[k][i][1]);
			StemOwn[k][i] = pStem->IsOwn(CHANID_VRC7_CH1 + i);
		}
	}

	// Generate VRC7 samples, the left side and the right
	while (m_iBufferPtr < WantSamples) {
		int16 Raw[2];
		int32 Chan[6], Extra = 0;
		if (Stems > 0)
			OPLL_calc_lr_parts(m_pOPLLInt, GainL, GainR, &Raw[0], &Raw[1], Chan, &Extra);
		else if (AnyOwn)
			OPLL_calc_lr_ch(m_pOPLLInt, GainL, GainR, &Raw[0], &Raw[1], Chan);
		else
			OPLL_calc_lr(m_pOPLLInt, GainL, GainR, &Raw[0], &Raw[1]);

		for (int i = 0; i < 6 && AnyOwn; ++i) {
			if (!Own[i] || m_iBufferPtr >= m_iMaxSamples)
				continue;
			for (int s = 0; s < 2; ++s) {
				// As calc_lr ends, the clipping, the volume and the average with the sample before
				int32 RawSample = int16(int16((Chan[i] * OwnGain[i][s] + 128) >> 8) << 3);
				if (RawSample > 3600)
					RawSample = 3600;
				if (RawSample < -3200)
					RawSample = -3200;
				int32 Sample = int(float(RawSample) * m_fVolume);
				if (Sample > 32767)
					Sample = 32767;
				if (Sample < -32768)
					Sample = -32768;
				m_pOwn[i][s][m_iBufferPtr] = int16((Sample + m_iOwnLast[i][s]) >> 1);
				m_iOwnLast[i][s] = Sample;
			}
		}

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

		// Each stem's, from the channels' values, as calc_lr and the lines above make the mix's
		for (int k = 0; k < Stems && m_iBufferPtr < m_iMaxSamples; ++k) {
			for (int s = 0; s < 2; ++s) {
				int32 Sum = 0;
				for (int i = 0; i < 6; ++i)
					if (!StemOwn[k][i])
						Sum += Chan[i] * StemGain[k][i][s];
				StemBuffer(k, s)[m_iBufferPtr] = Finished(int16(int16(((Sum + 128) >> 8) + Extra) << 3), StemLast(k, s));
			}
			for (int i = 0; i < 6; ++i)
				for (int s = 0; s < 2 && StemOwn[k][i]; ++s)
					StemBuffer(k, 2 + 2 * i + s)[m_iBufferPtr] = Finished(int16(int16((Chan[i] * StemGain[k][i][s] + 128) >> 8) << 3),
						StemLast(k, 2 + 2 * i + s));
		}
		++m_iBufferPtr;
	}

	m_pMixer->MixSamples((blip_sample_t*)m_pBuffer, (blip_sample_t*)m_pBufferRight, WantSamples);
	for (int i = 0; i < 6 && AnyOwn; ++i)
		if (Own[i])
			m_pMixer->MixOwnSamples(CHANID_VRC7_CH1 + i, (blip_sample_t*)m_pOwn[i][0], (blip_sample_t*)m_pOwn[i][1], WantSamples);
	for (int k = 0; k < Stems; ++k) {
		CMixer *pStem = m_pMixer->Stem(k);
		pStem->MixSamples((blip_sample_t*)StemBuffer(k, 0), (blip_sample_t*)StemBuffer(k, 1), WantSamples);
		for (int i = 0; i < 6; ++i)
			if (StemOwn[k][i])
				pStem->MixOwnSamples(CHANID_VRC7_CH1 + i, (blip_sample_t*)StemBuffer(k, 2 + 2 * i), (blip_sample_t*)StemBuffer(k, 3 + 2 * i),
					WantSamples);
	}

	m_iBufferPtr -= WantSamples;
	m_iTime = 0;
}

void CVRC7::Process(uint32 Time)
{
	// This cannot run in sync, fetch all samples at end of frame instead
	m_iTime += Time;
}

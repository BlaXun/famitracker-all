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

/*

 This will mix and synthesize the APU audio using blargg's blip-buffer

 Mixing of internal audio relies on Blargg's findings

 Mixing of external channles are based on my own research:

 VRC6 (Madara): 
	Pulse channels has the same amplitude as internal-
    pulse channels on equal volume levels.

 FDS: 
	Square wave @ v = $1F: 2.4V
	  			  v = $0F: 1.25V
	(internal square wave: 1.0V)

 MMC5 (just breed): 
	2A03 square @ v = $0F: 760mV (the cart attenuates internal channels a little)
	MMC5 square @ v = $0F: 900mV

 VRC7:
	2A03 Square  @ v = $0F: 300mV (the cart attenuates internal channels a lot)
	VRC7 Patch 5 @ v = $0F: 900mV
	Did some more tests and found patch 14 @ v=15 to be 13.77dB stronger than a 50% square @ v=15

 ---

 N163 is still unknown

 5B: the real cartridge is very loud. Here a 5B square at volume 15 is as loud
 as a 2A03 pulse of 50% duty at 15 (SynthS5B), as the VRC6's pulses are, and the
 other chips make room for it as they do for the VRC6.

 Stereo (this FamiTracker's own; the NES plays in mono): every channel has a
 pan (the 8xx effect, CChannelHandler::SetPan), 0x00 left, 0x80 the middle,
 0xFF right, and the mixer renders a left and a right side, a Blip_Buffer
 each. A channel's level on a side is its level times the side's gain
 (PanGains): both gains are 1 in the middle, so a song that doesn't pan comes
 out on both sides sample for sample as the mono mix did. The 2A03's two
 outputs mix their channels nonlinearly (CalcPin1, CalcPin2): each side mixes
 its share of every channel with the same formula. The linear chips' channels
 (VRC6, MMC5, FDS, and the 5B's, whose levels CS5B sends as they change) send
 each side its share of their level; the N163's one DAC plays its channels in
 turn, each with its own pan. The VRC7 makes its samples at the end of a
 frame, a side at a time with the pans then (CVRC7::EndFrame).

 Depth (this FamiTracker's own too, Space.h): a channel with Soft or Space
 on is mixed into two Blip_Buffers of its own instead of the sides (the
 2A03's channels as their share of their pin, the N163's as the DAC's level
 while it plays them, the VRC7's as samples of their own), and ReadBuffer
 adds them to the sides, softened, sending them to the space, whose sound
 it adds too. Its buffers stay in step with the sides' (the same clock, the
 same frames), and when a channel leaves them what it sent there moves back
 to the sides at that moment; they're read for a second more while what
 they had dies away. With every channel's Soft and Space at 0 and the space
 silent, ReadBuffer is what it was, sample for sample.

*/

#include "../stdafx.h"
#include <algorithm>
#include <memory>
#include <cmath>
#include "Mixer.h"
#include "APU.h"
#include "emu2413.h"
#include "emu2149.h"

//#define LINEAR_MIXING

static const double AMP_2A03 = 400.0;

static const float LEVEL_FALL_OFF_RATE	= 0.6f;
static const int   LEVEL_FALL_OFF_DELAY = 3;

CMixer::CMixer()
{
	memset(m_iChannels, 0, sizeof(int32) * CHANNELS);
	memset(m_fChannelLevels, 0, sizeof(float) * CHANNELS);
	memset(m_iChanLevelFallOff, 0, sizeof(uint32) * CHANNELS);

	for (int i = 0; i < CHANNELS; ++i) {
		m_iPan[i] = 0x80;
		m_dGain[i][0] = m_dGain[i][1] = 1.0;
		m_dLevel[i] = 1.0;
		m_dSide[i][0] = m_dSide[i][1] = 1.0;
		m_iOwnSent[i][0] = m_iOwnSent[i][1] = 0;
		m_iLevel[i] = 0;
		m_iSent[i][0] = m_iSent[i][1] = 0;
		m_bSilent[i] = false;
	}
	m_iN163Level = 0;
	m_iN163Chan = CHANID_N163_CHAN1;
	m_iN163Sent[0] = m_iN163Sent[1] = 0;
	m_fVolumeVRC6 = m_fVolumeMMC5 = 1.0f;

	m_fLevelAPU1 = 1.0f;
	m_fLevelAPU2 = 1.0f;
	m_fLevelVRC6 = 1.0f;
	m_fLevelMMC5 = 1.0f;
	m_fLevelFDS = 1.0f;
	m_fLevelN163 = 1.0f;
	m_fLevelS5B = 1.0f;

	m_iExternalChip = 0;
	m_iSampleRate = 0;
	m_iLowCut = 0;
	m_iHighCut = 0;
	m_iHighDamp = 0;
	m_fOverallVol = 1.0f;

	m_dSumSS[0] = m_dSumSS[1] = 0.0;
	m_dSumTND[0] = m_dSumTND[1] = 0.0;

	for (int i = 0; i < CHANNELS; ++i) {
		m_bOwn[i] = false;
		m_iOwnDrain[i] = 0;
		m_iSoftOf[i] = 0;
		m_iSpaceOf[i] = 0;
		m_iPinOwn[i][0] = m_iPinOwn[i][1] = 0;
	}
	m_iOwnCount = 0;
	m_iN163Dest = -1;
	for (int i = 0; i < 4; ++i)
		m_pDepth[i] = NULL;
	m_pDepthRead = NULL;
	m_iDepthSize = 0;

	for (int k = 0; k < MAX_STEMS; ++k) {
		m_pStem[k] = NULL;
		for (int i = 0; i < CHANNELS; ++i)
			m_bStemSilent[k][i] = false;
	}
	m_iStemCount = 0;
	m_bStem = false;
	m_iBufferLength = 0;
	m_iNrChannels = 2;
}

CMixer::~CMixer()
{
	FreeStems();
	FreeDepth();
}

void CMixer::FreeDepth()
{
	for (int i = 0; i < 4; ++i) {
		delete [] m_pDepth[i];
		m_pDepth[i] = NULL;
	}
	delete [] m_pDepthRead;
	m_pDepthRead = NULL;
	m_iDepthSize = 0;
}

// A channel's level on a side: exact in the middle, where the gain is 1 (and nothing at a gain of 0, as the rounding has it)
static inline int32 SideLevel(int32 Level, double Gain)
{
	return Gain == 1.0 ? Level : Gain == 0.0 ? 0 : static_cast<int32>(std::floor(Level * Gain + 0.5));
}

inline double CMixer::CalcPin1(double Val1, double Val2)
{
	// Mix the output of APU audio pin 1: square
	//

	if ((Val1 + Val2) > 0)
		return 95.88 / ((8128.0 / (Val1 + Val2)) + 100.0);

	return 0;
}

inline double CMixer::CalcPin2(double Val1, double Val2, double Val3)
{
	// Mix the output of APU audio pin 2: triangle, noise and DPCM
	//

	if ((Val1 + Val2 + Val3) > 0)
		return 159.79 / ((1.0 / ((Val1 / 8227.0) + (Val2 / 12241.0) + (Val3 / 22638.0))) + 100.0);

	return 0;
}

void CMixer::ExternalSound(int Chip)
{
	m_iExternalChip = Chip;
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->m_iExternalChip = Chip;		// UpdateSettings tells them the rest
	UpdateSettings(m_iLowCut, m_iHighCut, m_iHighDamp, m_fOverallVol);
}

void CMixer::SetChipLevel(chip_level_t Chip, float Level)
{
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->SetChipLevel(Chip, Level);
	switch (Chip) {
		case CHIP_LEVEL_APU1:
			m_fLevelAPU1 = Level;
			break;
		case CHIP_LEVEL_APU2:
			m_fLevelAPU2 = Level;
			break;
		case CHIP_LEVEL_VRC6:
			m_fLevelVRC6 = Level;
			break;
		case CHIP_LEVEL_MMC5:
			m_fLevelMMC5 = Level;
			break;
		case CHIP_LEVEL_FDS:
			m_fLevelFDS = Level;
			break;
		case CHIP_LEVEL_N163:
			m_fLevelN163 = Level;
			break;
		case CHIP_LEVEL_S5B:
			m_fLevelS5B = Level;
			break;
	}
}

float CMixer::GetAttenuation() const
{
	const float ATTENUATION_VRC6 = 0.80f;
	const float ATTENUATION_VRC7 = 0.64f;
	const float ATTENUATION_N163 = 0.70f;
	const float ATTENUATION_MMC5 = 0.83f;
	const float ATTENUATION_FDS  = 0.90f;
	const float ATTENUATION_S5B  = 0.80f;

	float Attenuation = 1.0f;

	// Increase headroom if some expansion chips are enabled

	if (m_iExternalChip & SNDCHIP_VRC7)
		Attenuation *= ATTENUATION_VRC7;

	if (m_iExternalChip & SNDCHIP_N163)
		Attenuation *= ATTENUATION_N163;

	if (m_iExternalChip & SNDCHIP_VRC6)
		Attenuation *= ATTENUATION_VRC6;

	if (m_iExternalChip & SNDCHIP_MMC5)
		Attenuation *= ATTENUATION_MMC5;

	if (m_iExternalChip & SNDCHIP_FDS)
		Attenuation *= ATTENUATION_FDS;

	if (m_iExternalChip & SNDCHIP_S5B)
		Attenuation *= ATTENUATION_S5B;

	return Attenuation;
}

void CMixer::UpdateSettings(int LowCut,	int HighCut, int HighDamp, float OverallVol)
{
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->UpdateSettings(LowCut, HighCut, HighDamp, OverallVol);

	float Volume = OverallVol * GetAttenuation();

	// Blip-buffer filtering
	BlipBuffer.bass_freq(LowCut);
	BlipBufferRight.bass_freq(LowCut);
	for (int i = 0; i < CHANNELS; ++i) {
		m_OwnBuffer[i][0].bass_freq(LowCut);
		m_OwnBuffer[i][1].bass_freq(LowCut);
	}

	blip_eq_t eq(-HighDamp, HighCut, m_iSampleRate);

	Synth2A03SS.treble_eq(eq);
	Synth2A03TND.treble_eq(eq);
	SynthVRC6.treble_eq(eq);
	SynthMMC5.treble_eq(eq);
	SynthS5B.treble_eq(eq);
	for (int i = 0; i < 3; ++i) {
		SynthVRC6Own[i].treble_eq(eq);
		SynthMMC5Own[i].treble_eq(eq);
	}

	// N163 special filtering
	double n163_treble = 24;
	long n163_rolloff = 12000;

	if (HighDamp > n163_treble)
		n163_treble = HighDamp;

	if (n163_rolloff > HighCut)
		n163_rolloff = HighCut;

	blip_eq_t eq_n163(-n163_treble, n163_rolloff, m_iSampleRate);
	SynthN163.treble_eq(eq_n163);

	// FDS special filtering (TODO fix this for high sample rates)
	blip_eq_t fds_eq(-48, 1000, m_iSampleRate);

	SynthFDS.treble_eq(fds_eq);

	// Volume levels
	Synth2A03SS.volume(Volume * m_fLevelAPU1);
	Synth2A03TND.volume(Volume * m_fLevelAPU2);
	m_fVolumeVRC6 = Volume * 3.98333f * m_fLevelVRC6;
	SynthVRC6.volume(m_fVolumeVRC6);
	SynthFDS.volume(Volume * 1.00f * m_fLevelFDS);
	m_fVolumeMMC5 = Volume * 1.18421f * m_fLevelMMC5;
	SynthMMC5.volume(m_fVolumeMMC5);
	
	// Not checked
	SynthN163.volume(Volume * 1.1f * m_fLevelN163);

	// 5B: a channel at 15 (255) as a 2A03 pulse at 15 (CalcPin1 * AMP_2A03 = 59.75 of Synth2A03SS's 500)
	SynthS5B.volume(Volume * 0.9373f * m_fLevelS5B);
	SetOwnVolumes();

	m_iLowCut = LowCut;
	m_iHighCut = HighCut;
	m_iHighDamp = HighDamp;
	m_fOverallVol = OverallVol;
}

void CMixer::SetNamcoVolume(float fVol)
{
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->SetNamcoVolume(fVol);

	float fVolume = fVol * m_fOverallVol * GetAttenuation();

	SynthN163.volume(fVolume * 1.1f * m_fLevelN163);
}

void CMixer::SetOwnVolumes()
{
	for (int i = 0; i < 3; ++i) {
		SynthVRC6Own[i].volume(m_fVolumeVRC6 * m_dLevel[CHANID_VRC6_PULSE1 + i]);
		SynthMMC5Own[i].volume(m_fVolumeMMC5 * m_dLevel[CHANID_MMC5_SQUARE1 + i]);
	}
}

void CMixer::MixSamples(blip_sample_t *pBuffer, uint32 Count)
{
	// Samples in the middle, on both sides
	BlipBuffer.mix_samples(pBuffer, Count);
	BlipBufferRight.mix_samples(pBuffer, Count);
}

void CMixer::MixSamples(blip_sample_t *pLeft, blip_sample_t *pRight, uint32 Count)
{
	// For VRC7 and 5B
	BlipBuffer.mix_samples(pLeft, Count);
	BlipBufferRight.mix_samples(pRight, Count);
}

uint32 CMixer::GetMixSampleCount(int t) const
{
	return BlipBuffer.count_samples(t);
}

bool CMixer::AllocateBuffer(unsigned int BufferLength, uint32 SampleRate, uint8 NrChannels)
{
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->AllocateBuffer(BufferLength, SampleRate, NrChannels);

	m_iSampleRate = SampleRate;
	m_iBufferLength = BufferLength;
	m_iNrChannels = NrChannels;
	BlipBuffer.sample_rate(SampleRate, (BufferLength * 1000 * 2) / SampleRate);
	BlipBufferRight.sample_rate(SampleRate, (BufferLength * 1000 * 2) / SampleRate);

	// Depth: every channel's own buffers, the space and ReadDepth's room, all made here (not while sound plays)
	for (int i = 0; i < CHANNELS; ++i) {
		for (int s = 0; s < 2; ++s) {
			m_OwnBuffer[i][s].sample_rate(SampleRate, (BufferLength * 1000 * 2) / SampleRate);
			m_OwnBuffer[i][s].clock_rate(BlipBuffer.clock_rate());
			m_OwnBuffer[i][s].bass_freq(m_iLowCut);
		}
		m_SoftFilter[i].Set(m_iSoftOf[i], SampleRate);
	}
	m_Space.Allocate(SampleRate);
	FreeDepth();
	m_iDepthSize = BlipBuffer.buffer_size_ + 64;
	for (int i = 0; i < 4; ++i)
		m_pDepth[i] = new float[m_iDepthSize];
	m_pDepthRead = new blip_sample_t[m_iDepthSize * 2];
	return true;
}

void CMixer::SetClockRate(uint32 Rate)
{
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->SetClockRate(Rate);

	// Change the clockrate
	BlipBuffer.clock_rate(Rate);
	BlipBufferRight.clock_rate(Rate);
	for (int i = 0; i < CHANNELS; ++i) {
		m_OwnBuffer[i][0].clock_rate(Rate);
		m_OwnBuffer[i][1].clock_rate(Rate);
	}
}

void CMixer::ClearBuffer()
{
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->ClearBuffer();

	BlipBuffer.clear();
	BlipBufferRight.clear();

	// The channels' levels stay as they are, as the mono mixer kept them
	m_dSumSS[0] = m_dSumSS[1] = 0;
	m_dSumTND[0] = m_dSumTND[1] = 0;

	// Depth: the channels' own buffers empty as the sides; the space rings on (a song that stops or halts keeps
	// its tail), until ClearSpace
	for (int i = 0; i < CHANNELS; ++i) {
		if (OwnActive(i)) {
			m_OwnBuffer[i][0].clear();
			m_OwnBuffer[i][1].clear();
		}
		m_iPinOwn[i][0] = m_iPinOwn[i][1] = 0;
		m_SoftFilter[i].Clear();
	}
}

void CMixer::ClearSpace()
{
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->ClearSpace();
	m_Space.Clear();
}

int CMixer::SamplesAvail() const
{	
	return (int)BlipBuffer.samples_avail();
}

int CMixer::FinishBuffer(int t)
{
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->FinishBuffer(t);

	BlipBuffer.end_frame(t);
	BlipBufferRight.end_frame(t);
	for (int i = 0; i < CHANNELS && m_iOwnCount > 0; ++i)
		if (OwnActive(i)) {
			m_OwnBuffer[i][0].end_frame(t);
			m_OwnBuffer[i][1].end_frame(t);
		}

	// Get channel levels for VRC7 (the meters are the main mixer's, not a stem's)
	for (int i = 0; i < 6 && !m_bStem; ++i)
		StoreChannelLevel(CHANID_VRC7_CH1 + i, OPLL_getchanvol(i));

	// Get channel levels for Sunsoft
	for (int i = 0; i < 3 && !m_bStem; ++i)
		StoreChannelLevel(CHANID_S5B_CH1 + i, PSG_getchanvol(i));

	for (int i = 0; i < CHANNELS && !m_bStem; ++i) {
		if (m_iChanLevelFallOff[i] > 0)
			m_iChanLevelFallOff[i]--;
		else {
			if (m_fChannelLevels[i] > 0) {
				m_fChannelLevels[i] -= LEVEL_FALL_OFF_RATE;
				if (m_fChannelLevels[i] < 0)
					m_fChannelLevels[i] = 0;
			}
		}
	}

	// Return number of samples available
	return BlipBuffer.samples_avail();
}

//
// Mixing
//

// The first pin's output from the pulses' inputs on a side, the mixer window's levels in: each pulse's share of the pin
// (in proportion to its input, as the formula is) scaled, so the pin compresses them as it did
double CMixer::Pin1(double Pulse1, double Pulse2)
{
	double Sum = CalcPin1(Pulse1, Pulse2);
	if ((m_dLevel[CHANID_SQUARE1] != 1.0 || m_dLevel[CHANID_SQUARE2] != 1.0) && Pulse1 + Pulse2 > 0)
		Sum *= (Pulse1 * m_dLevel[CHANID_SQUARE1] + Pulse2 * m_dLevel[CHANID_SQUARE2]) / (Pulse1 + Pulse2);
	return Sum;
}

// ...and the second pin's, each channel's share scaled as on the first
double CMixer::Pin2(double Triangle, double Noise, double Dpcm)
{
	double Sum = CalcPin2(Triangle, Noise, Dpcm);
	if (m_dLevel[CHANID_TRIANGLE] != 1.0 || m_dLevel[CHANID_NOISE] != 1.0 || m_dLevel[CHANID_DPCM] != 1.0) {
		const double Shares = Triangle / 8227.0 + Noise / 12241.0 + Dpcm / 22638.0;
		if (Shares > 0)
			Sum *= (Triangle / 8227.0 * m_dLevel[CHANID_TRIANGLE] + Noise / 12241.0 * m_dLevel[CHANID_NOISE] +
				Dpcm / 22638.0 * m_dLevel[CHANID_DPCM]) / Shares;
	}
	return Sum;
}

void CMixer::MixInternal1(int Time)
{
	// Both pulses left out, with nothing on the sides of them (a stem of other channels): nothing to mix
	if (m_bSilent[CHANID_SQUARE1] && m_bSilent[CHANID_SQUARE2] && m_dSumSS[0] == 0.0 && m_dSumSS[1] == 0.0 && !m_bOwn[CHANID_SQUARE1] &&
		!m_bOwn[CHANID_SQUARE2] && m_iPinOwn[CHANID_SQUARE1][0] == 0 && m_iPinOwn[CHANID_SQUARE1][1] == 0 && m_iPinOwn[CHANID_SQUARE2][0] == 0 &&
		m_iPinOwn[CHANID_SQUARE2][1] == 0)
		return;
	// Each side mixes its share of the pulses (a pulse in the middle has all of it on both)
	for (int s = 0; s < 2; ++s) {
#ifdef LINEAR_MIXING
		SumL = ((m_iChannels[CHANID_SQUARE1].Left + m_iChannels[CHANID_SQUARE2].Left) * 0.00752) * InternalVol;
		SumR = ((m_iChannels[CHANID_SQUARE1].Right + m_iChannels[CHANID_SQUARE2].Right) *  0.00752) * InternalVol;
#else
		const double Pulse1 = m_iChannels[CHANID_SQUARE1] * m_dGain[CHANID_SQUARE1][s], Pulse2 = m_iChannels[CHANID_SQUARE2] * m_dGain[CHANID_SQUARE2][s];
		double Sum = Pin1(Pulse1, Pulse2);
		// A render of some parts alone (SetChannelSilent): what the pin plays from the pulses left out alone comes off,
		// so the rest is the other's sound in the mix, the pin's compression in
		const bool Quiet1 = m_bSilent[CHANID_SQUARE1], Quiet2 = m_bSilent[CHANID_SQUARE2];
		if (Quiet1 || Quiet2)
			Sum = Quiet1 && Quiet2 ? 0.0 : Sum - Pin1(Quiet1 ? Pulse1 : 0.0, Quiet2 ? Pulse2 : 0.0);
#endif

		double Delta = (Sum - m_dSumSS[s]) * AMP_2A03;
		int Step = (int)Delta;
		if (m_bOwn[CHANID_SQUARE1] || m_bOwn[CHANID_SQUARE2] || m_iPinOwn[CHANID_SQUARE1][s] != 0 || m_iPinOwn[CHANID_SQUARE2][s] != 0) {
			// Depth: a pulse in its own buffers gets its share of the pin there, out of the sides' step
			static const int CHANS[2] = { CHANID_SQUARE1, CHANID_SQUARE2 };
			const double Weighted[2] = { Quiet1 ? 0.0 : Pulse1 * m_dLevel[CHANID_SQUARE1], Quiet2 ? 0.0 : Pulse2 * m_dLevel[CHANID_SQUARE2] };
			Step -= SplitPin(Synth2A03SS, CHANS, Weighted, 2, Sum, s, Time);
		}
		if (Step != 0)
			Synth2A03SS.offset(Time, Step, &Side(s));
		m_dSumSS[s] = Sum;
	}
}

void CMixer::MixInternal2(int Time, bool Send)
{
	// The three left out, as on the first pin
	if (m_bSilent[CHANID_TRIANGLE] && m_bSilent[CHANID_NOISE] && m_bSilent[CHANID_DPCM] && m_dSumTND[0] == 0.0 && m_dSumTND[1] == 0.0) {
		bool Own = false;
		for (int c = CHANID_TRIANGLE; c <= CHANID_DPCM && !Own; ++c)
			Own = m_bOwn[c] || m_iPinOwn[c][0] != 0 || m_iPinOwn[c][1] != 0;
		if (!Own)
			return;
	}
	for (int s = 0; s < 2; ++s) {
#ifdef LINEAR_MIXING
		SumL = ((0.00851 * m_iChannels[CHANID_TRIANGLE].Left + 0.00494 * m_iChannels[CHANID_NOISE].Left + 0.00335 * m_iChannels[CHANID_DPCM].Left)) * InternalVol;
		SumR = ((0.00851 * m_iChannels[CHANID_TRIANGLE].Right + 0.00494 * m_iChannels[CHANID_NOISE].Right + 0.00335 * m_iChannels[CHANID_DPCM].Right)) * InternalVol;
#else
		const double Triangle = m_iChannels[CHANID_TRIANGLE] * m_dGain[CHANID_TRIANGLE][s], Noise = m_iChannels[CHANID_NOISE] * m_dGain[CHANID_NOISE][s],
			Dpcm = m_iChannels[CHANID_DPCM] * m_dGain[CHANID_DPCM][s];
		double Sum = Pin2(Triangle, Noise, Dpcm);
		// A render of some parts alone, as on the first pin: a DPCM level left out still softens the triangle and the noise
		const bool QuietT = m_bSilent[CHANID_TRIANGLE], QuietN = m_bSilent[CHANID_NOISE], QuietD = m_bSilent[CHANID_DPCM];
		if (QuietT || QuietN || QuietD)
			Sum = QuietT && QuietN && QuietD ? 0.0 : Sum - Pin2(QuietT ? Triangle : 0.0, QuietN ? Noise : 0.0, QuietD ? Dpcm : 0.0);
#endif

		double Delta = (Sum - m_dSumTND[s]) * AMP_2A03;
		int Step = (int)Delta;
		if (m_bOwn[CHANID_TRIANGLE] || m_bOwn[CHANID_NOISE] || m_bOwn[CHANID_DPCM] || m_iPinOwn[CHANID_TRIANGLE][s] != 0 ||
			m_iPinOwn[CHANID_NOISE][s] != 0 || m_iPinOwn[CHANID_DPCM][s] != 0) {
			static const int CHANS[3] = { CHANID_TRIANGLE, CHANID_NOISE, CHANID_DPCM };
			const double Weighted[3] = { QuietT ? 0.0 : Triangle / 8227.0 * m_dLevel[CHANID_TRIANGLE], QuietN ? 0.0 : Noise / 12241.0 *
				m_dLevel[CHANID_NOISE], QuietD ? 0.0 : Dpcm / 22638.0 * m_dLevel[CHANID_DPCM] };
			Step -= SplitPin(Synth2A03TND, CHANS, Weighted, 3, Sum, s, Time, Send);
		}
		if (Send && Step != 0)
			Synth2A03TND.offset(Time, Step, &Side(s));
		m_dSumTND[s] = Sum;
	}
}

void CMixer::StartAt(int ChanID, int Value)
{
	// The pin's output with the channel at Value, taken as where it is: nothing sent, so the sound starts there without
	// a step (a pop through the low cut), and the triangle and the noise play softened by the DPCM's level from the start
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->StartAt(ChanID, Value);
	if (ChanID != CHANID_TRIANGLE && ChanID != CHANID_NOISE && ChanID != CHANID_DPCM)
		return;
	m_iChannels[ChanID] = Value;
	MixInternal2(0, false);
}

// A linear chip's channel: each side gets its share of the channel's level
template <class Synth>
void CMixer::MixSides(Synth &Synth_, int Dest, int32 Level, const double *pGain, int32 *pSent, int Time)
{
	for (int s = 0; s < 2; ++s) {
		const int32 Want = SideLevel(Level, pGain[s]);
		if (Want != pSent[s]) {
			Synth_.offset(Time, Want - pSent[s], &Buffer(Dest, s));
			pSent[s] = Want;
		}
	}
}

// ...and where the mixer window's level lives in a synth of the channel's own: through that one when Faded (off 0 dB),
// through the chip's otherwise (the levels move between the two as the fader leaves 0 dB or comes back)
template <class Synth>
void CMixer::MixSides(Synth &Synth_, Synth &Own, bool Faded, int Dest, int32 Level, const double *pGain, int32 *pSent, int32 *pOwnSent,
	int Time)
{
	for (int s = 0; s < 2; ++s) {
		const int32 Want = SideLevel(Level, pGain[s]);
		const int32 Chip = Faded ? 0 : Want, Mine = Faded ? Want : 0;
		if (Chip != pSent[s]) {
			Synth_.offset(Time, Chip - pSent[s], &Buffer(Dest, s));
			pSent[s] = Chip;
		}
		if (Mine != pOwnSent[s]) {
			Own.offset(Time, Mine - pOwnSent[s], &Buffer(Dest, s));
			pOwnSent[s] = Mine;
		}
	}
}

void CMixer::MixN163(int ChanID, int Value, int Time)
{
	// The DAC's level changed (Value), and it plays ChanID now
	m_iN163Level += Value;
	m_iN163Chan = ChanID;
	MixN163Sides(Time);
}

void CMixer::N163Channel(int ChanID, int Time)
{
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->N163Channel(ChanID, Time);
	if (ChanID == m_iN163Chan)
		return;
	m_iN163Chan = ChanID;
	if (m_dSide[ChanID][0] != 1.0 || m_dSide[ChanID][1] != 1.0 || m_iN163Sent[0] != m_iN163Level || m_iN163Sent[1] != m_iN163Level ||
		DestOf(ChanID) != m_iN163Dest)
		MixN163Sides(Time);
}

void CMixer::MixN163Sides(int Time)
{
	// Depth: the DAC's level is in the buffers of the channel it plays (the sides, or the channel's own)
	const int Dest = DestOf(m_iN163Chan);
	// A channel at a level of nothing (left out of a stem), where nothing is sent: nothing to do
	if (Dest == m_iN163Dest && m_dSide[m_iN163Chan][0] == 0.0 && m_dSide[m_iN163Chan][1] == 0.0 && m_iN163Sent[0] == 0 &&
		m_iN163Sent[1] == 0)
		return;
	if (Dest != m_iN163Dest) {
		for (int s = 0; s < 2; ++s) {
			if (m_iN163Sent[s] != 0)
				SynthN163.offset(Time, -m_iN163Sent[s], &Buffer(m_iN163Dest, s));
			m_iN163Sent[s] = 0;
		}
		m_iN163Dest = Dest;
	}
	MixSides(SynthN163, Dest, m_iN163Level, m_dSide[m_iN163Chan], m_iN163Sent, Time);
}

void CMixer::MixFDS(int ChanID, int Value, int Time)
{
	m_iLevel[ChanID] += Value;
	MixSides(SynthFDS, DestOf(ChanID), m_iLevel[ChanID], m_dSide[ChanID], m_iSent[ChanID], Time);
}

void CMixer::MixVRC6(int ChanID, int Value, int Time)
{
	m_iLevel[ChanID] += Value;
	MixSides(SynthVRC6, SynthVRC6Own[ChanID - CHANID_VRC6_PULSE1], m_dLevel[ChanID] != 1.0, DestOf(ChanID), m_iLevel[ChanID], m_dGain[ChanID],
		m_iSent[ChanID], m_iOwnSent[ChanID], Time);
}

void CMixer::MixMMC5(int ChanID, int Value, int Time)
{
	m_iLevel[ChanID] += Value;
	MixSides(SynthMMC5, SynthMMC5Own[ChanID - CHANID_MMC5_SQUARE1], m_dLevel[ChanID] != 1.0, DestOf(ChanID), m_iLevel[ChanID], m_dGain[ChanID],
		m_iSent[ChanID], m_iOwnSent[ChanID], Time);
}

void CMixer::MixS5B(int ChanID, int Value, int Time)
{
	// Each channel's level as it changes (CS5B), on each side as its pan has it
	m_iLevel[ChanID] += Value;
	MixSides(SynthS5B, DestOf(ChanID), m_iLevel[ChanID], m_dSide[ChanID], m_iSent[ChanID], Time);
}

void CMixer::PanGains(int Pan, double &Left, double &Right)
{
	// 0x00 hard left, 0x80 the middle, 0xFF hard right: the side the sound moves
	// away from fades out, the other keeps all of it
	Left = Right = 1.0;
	if (Pan < 0x80)
		Right = std::max(0, Pan) / 128.0;
	else if (Pan > 0x80)
		Left = (0xFF - std::min(0xFF, Pan)) / 127.0;
}

void CMixer::SetChannelPan(int ChanID, int Pan, int Time)
{
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->SetChannelPan(ChanID, Pan, Time);
	if (ChanID < 0 || ChanID >= CHANNELS)
		return;
	Pan = std::max(0, std::min(0xFF, Pan));
	if (Pan == m_iPan[ChanID])
		return;
	m_iPan[ChanID] = Pan;
	PanGains(Pan, m_dGain[ChanID][0], m_dGain[ChanID][1]);
	Remix(ChanID, Time);
}

void CMixer::SetChannelGain(int ChanID, double Gain, int Time)
{
	// A stem's channels left out of it stay at a level of nothing, but the 2A03's, which SetChannelSilent leaves out
	for (int k = 0; k < m_iStemCount && ChanID >= 0 && ChanID < CHANNELS; ++k)
		m_pStem[k]->SetChannelGain(ChanID, m_bStemSilent[k][ChanID] && ChanID > CHANID_DPCM ? 0.0 : Gain, Time);
	if (ChanID < 0 || ChanID >= CHANNELS || !(Gain >= 0.0) || Gain == m_dLevel[ChanID])
		return;
	// The channel's own synth changes its volume: what it has comes out at the old one first, and goes back in at
	// the new one (Remix)
	ClearOwn(ChanID, Time);
	m_dLevel[ChanID] = Gain;
	SetOwnVolumes();
	Remix(ChanID, Time);
}

void CMixer::ClearOwn(int ChanID, int Time)
{
	for (int s = 0; s < 2; ++s) {
		const int32 Had = m_iOwnSent[ChanID][s];
		if (Had == 0)
			continue;
		if (ChanID >= CHANID_VRC6_PULSE1 && ChanID <= CHANID_VRC6_SAWTOOTH)
			SynthVRC6Own[ChanID - CHANID_VRC6_PULSE1].offset(Time, -Had, &Buffer(DestOf(ChanID), s));
		else if (ChanID >= CHANID_MMC5_SQUARE1 && ChanID <= CHANID_MMC5_VOICE)
			SynthMMC5Own[ChanID - CHANID_MMC5_SQUARE1].offset(Time, -Had, &Buffer(DestOf(ChanID), s));
		m_iOwnSent[ChanID][s] = 0;
	}
}

void CMixer::SetChannelSilent(int ChanID, bool Silent, int Time)
{
	// The 2A03's channels: the others mix their channels apart, and a level of nothing silences them
	for (int k = 0; k < m_iStemCount && ChanID >= CHANID_SQUARE1 && ChanID <= CHANID_DPCM; ++k)
		m_pStem[k]->SetChannelSilent(ChanID, Silent || m_bStemSilent[k][ChanID], Time);
	if (ChanID < CHANID_SQUARE1 || ChanID > CHANID_DPCM || m_bSilent[ChanID] == Silent)
		return;
	m_bSilent[ChanID] = Silent;
	Remix(ChanID, Time);
}

double CMixer::GetChannelGain(int ChanID) const
{
	return (ChanID >= 0 && ChanID < CHANNELS) ? m_dLevel[ChanID] : 1.0;
}

void CMixer::Remix(int ChanID, int Time)
{
	// The pan and the level together, for the FDS, N163, 5B and VRC7 (in the middle at 1: the pan's gains themselves);
	// the VRC6 and MMC5 play a level through the channel's own synth, the 2A03 through its share of the pin
	for (int s = 0; s < 2; ++s)
		m_dSide[ChanID][s] = m_dLevel[ChanID] == 1.0 ? m_dGain[ChanID][s] : m_dGain[ChanID][s] * m_dLevel[ChanID];

	// What the channel sounds moves to its new place now
	switch (ChanID) {
		case CHANID_SQUARE1:
		case CHANID_SQUARE2:
			MixInternal1(Time);
			break;
		case CHANID_TRIANGLE:
		case CHANID_NOISE:
		case CHANID_DPCM:
			MixInternal2(Time);
			break;
		case CHANID_VRC6_PULSE1:
		case CHANID_VRC6_PULSE2:
		case CHANID_VRC6_SAWTOOTH:
			MixVRC6(ChanID, 0, Time);
			break;
		case CHANID_MMC5_SQUARE1:
		case CHANID_MMC5_SQUARE2:
		case CHANID_MMC5_VOICE:
			MixMMC5(ChanID, 0, Time);
			break;
		case CHANID_FDS:
			MixSides(SynthFDS, DestOf(ChanID), m_iLevel[ChanID], m_dSide[ChanID], m_iSent[ChanID], Time);
			break;
		default:
			if (ChanID >= CHANID_N163_CHAN1 && ChanID <= CHANID_N163_CHAN8 && ChanID == m_iN163Chan)
				MixN163Sides(Time);
			if (ChanID >= CHANID_S5B_CH1 && ChanID <= CHANID_S5B_CH3)
				MixS5B(ChanID, 0, Time);
			// The VRC7's channels: their samples are made at the end of the frame
			break;
	}
}

int CMixer::GetChannelPan(int ChanID) const
{
	return (ChanID >= 0 && ChanID < CHANNELS) ? m_iPan[ChanID] : 0x80;
}

void CMixer::PanGains256(int ChanID, int32 &Left, int32 &Right) const
{
	Left = Right = 256;
	if (ChanID < 0 || ChanID >= CHANNELS)
		return;
	Left = static_cast<int32>(std::floor(m_dSide[ChanID][0] * 256.0 + 0.5));
	Right = static_cast<int32>(std::floor(m_dSide[ChanID][1] * 256.0 + 0.5));
}

void CMixer::AddValue(int ChanID, int Chip, int Value, int AbsValue, int FrameCycles)
{
	// Add sound to mixer
	//
	
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->AddValue(ChanID, Chip, Value, AbsValue, FrameCycles);

	int Delta = Value - m_iChannels[ChanID];
	if (!m_bStem)
		StoreChannelLevel(ChanID, AbsValue);
	m_iChannels[ChanID] = Value;

	switch (Chip) {
		case SNDCHIP_NONE:
			switch (ChanID) {
				case CHANID_SQUARE1:
				case CHANID_SQUARE2:
					MixInternal1(FrameCycles);
					break;
				case CHANID_TRIANGLE:
				case CHANID_NOISE:
				case CHANID_DPCM:
					MixInternal2(FrameCycles);
					break;
			}
			break;
		case SNDCHIP_N163:
			MixN163(ChanID, Value, FrameCycles);
			break;
		case SNDCHIP_FDS:
			MixFDS(ChanID, Value, FrameCycles);
			break;
		case SNDCHIP_MMC5:
			MixMMC5(ChanID, Delta, FrameCycles);
			break;
		case SNDCHIP_VRC6:
			MixVRC6(ChanID, Value, FrameCycles);
			break;
		case SNDCHIP_S5B:
			MixS5B(ChanID, Value, FrameCycles);
			break;
	}
}

int CMixer::ReadBuffer(int Size, void *Buffer, bool Stereo)
{
	blip_sample_t *pOut = (blip_sample_t*)Buffer;
	if (m_iOwnCount > 0 || !m_Space.Idle())
		return ReadDepth(Size, pOut, Stereo);
	if (Stereo) {
		// The left and the right side, one after the other
		const int Read = BlipBuffer.read_samples(pOut, Size, 1);
		BlipBufferRight.read_samples(pOut + 1, Size, 1);
		return Read;
	}
	// Mono: the two sides together, the left in Buffer and the right after it for a moment
	const int Read = BlipBuffer.read_samples(pOut, Size);
	BlipBufferRight.read_samples(pOut + Read, Read);
	for (int i = 0; i < Read; ++i)
		pOut[i] = static_cast<blip_sample_t>((pOut[i] + pOut[Read + i]) / 2);
	return Read;
}

int32 CMixer::GetChanOutput(uint8 Chan) const
{
	return (int32)m_fChannelLevels[Chan];
}

void CMixer::StoreChannelLevel(int Channel, int Value)
{
	int AbsVol = abs(Value);

	// Adjust channel levels for some channels
	if (Channel == CHANID_VRC6_SAWTOOTH)
		AbsVol = (AbsVol * 3) / 4;

	if (Channel == CHANID_DPCM)
		AbsVol /= 8;

	if (Channel == CHANID_FDS)
		AbsVol = AbsVol / 38;

	if (Channel >= CHANID_N163_CHAN1 && Channel <= CHANID_N163_CHAN8) {
		AbsVol /= 15;
		Channel = (7 - (Channel - CHANID_N163_CHAN1)) + CHANID_N163_CHAN1;
	}

	if (Channel >= CHANID_VRC7_CH1 && Channel <= CHANID_VRC7_CH6) {
		AbsVol = (int)(logf((float)AbsVol) * 3.0f);
	}

	if (Channel >= CHANID_S5B_CH1 && Channel <= CHANID_S5B_CH3) {
		AbsVol = (int)(logf((float)AbsVol) * 2.8f);
	}

	if (float(AbsVol) >= m_fChannelLevels[Channel]) {
		m_fChannelLevels[Channel] = float(AbsVol);
		m_iChanLevelFallOff[Channel] = LEVEL_FALL_OFF_DELAY;
	}
}

void CMixer::ClearChannelLevels()
{
	memset(m_fChannelLevels, 0, sizeof(float) * CHANNELS);
	memset(m_iChanLevelFallOff, 0, sizeof(uint32) * CHANNELS);
}

uint32 CMixer::ResampleDuration(uint32 Time) const
{
	return (uint32)BlipBuffer.resampled_duration((blip_time_t)Time);
}

// Depth

static const int OWN_DRAIN_FRAMES = 60;		// a second: what a channel had in its own buffers dies away by then

void CMixer::SetChannelDepth(int ChanID, int Soft, int Space, int Time)
{
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->SetChannelDepth(ChanID, Soft, Space, Time);
	if (ChanID < 0 || ChanID >= CHANNELS)
		return;
	Soft = std::max(0, std::min(100, Soft));
	Space = std::max(0, std::min(100, Space));
	m_iSpaceOf[ChanID] = Space;
	if (Soft != m_iSoftOf[ChanID]) {
		m_iSoftOf[ChanID] = Soft;
		m_SoftFilter[ChanID].Set(Soft, m_iSampleRate);
	}
	const bool Own = Soft > 0 || Space > 0;
	if (Own == m_bOwn[ChanID])
		return;
	if (Own) {
		if (m_iOwnDrain[ChanID] == 0) {
			// Its buffers start empty, in step with the sides
			for (int s = 0; s < 2; ++s) {
				m_OwnBuffer[ChanID][s].clear();
				m_OwnBuffer[ChanID][s].offset_ = Side(s).offset_;
			}
			m_SoftFilter[ChanID].Clear();
		}
		m_iOwnDrain[ChanID] = 0;
	}
	else
		m_iOwnDrain[ChanID] = OWN_DRAIN_FRAMES;
	m_bOwn[ChanID] = Own;
	MoveOwn(ChanID, Time);
	CountOwn();
}

void CMixer::SetSpace(int Room, double EchoSeconds, int Repeats)
{
	for (int k = 0; k < m_iStemCount; ++k)
		m_pStem[k]->SetSpace(Room, EchoSeconds, Repeats);
	m_Space.Set(Room, EchoSeconds, Repeats);
}

bool CMixer::SpaceRinging() const
{
	bool Ringing = !m_Space.Idle();
	for (int k = 0; k < m_iStemCount && !Ringing; ++k)
		Ringing = m_pStem[k]->SpaceRinging();
	return Ringing;
}

bool CMixer::IsOwn(int ChanID) const
{
	return ChanID >= 0 && ChanID < CHANNELS && m_bOwn[ChanID];
}

void CMixer::MixOwnSamples(int ChanID, blip_sample_t *pLeft, blip_sample_t *pRight, uint32 Count)
{
	if (ChanID < 0 || ChanID >= CHANNELS || !OwnActive(ChanID))
		return;
	m_OwnBuffer[ChanID][0].mix_samples(pLeft, Count);
	m_OwnBuffer[ChanID][1].mix_samples(pRight, Count);
}

void CMixer::CountOwn()
{
	m_iOwnCount = 0;
	for (int i = 0; i < CHANNELS; ++i)
		m_iOwnCount += OwnActive(i) ? 1 : 0;
}

template <class Synth>
void CMixer::Move(Synth &Synth_, int32 Amount, int From, int To, int Index, int Time)
{
	if (Amount == 0)
		return;
	Synth_.offset(Time, -Amount, &Buffer(From, Index));
	Synth_.offset(Time, Amount, &Buffer(To, Index));
}

void CMixer::MoveOwn(int ChanID, int Time)
{
	// What the channel sent so far leaves where it was for where its sound goes now, at once
	const int To = DestOf(ChanID), From = To < 0 ? ChanID : -1;
	for (int s = 0; s < 2; ++s) {
		if (ChanID >= CHANID_VRC6_PULSE1 && ChanID <= CHANID_VRC6_SAWTOOTH) {
			Move(SynthVRC6, m_iSent[ChanID][s], From, To, s, Time);
			Move(SynthVRC6Own[ChanID - CHANID_VRC6_PULSE1], m_iOwnSent[ChanID][s], From, To, s, Time);
		}
		else if (ChanID >= CHANID_MMC5_SQUARE1 && ChanID <= CHANID_MMC5_VOICE) {
			Move(SynthMMC5, m_iSent[ChanID][s], From, To, s, Time);
			Move(SynthMMC5Own[ChanID - CHANID_MMC5_SQUARE1], m_iOwnSent[ChanID][s], From, To, s, Time);
		}
		else if (ChanID == CHANID_FDS)
			Move(SynthFDS, m_iSent[ChanID][s], From, To, s, Time);
		else if (ChanID >= CHANID_S5B_CH1 && ChanID <= CHANID_S5B_CH3)
			Move(SynthS5B, m_iSent[ChanID][s], From, To, s, Time);
	}
	switch (ChanID) {
		case CHANID_SQUARE1:
		case CHANID_SQUARE2:
			MixInternal1(Time);
			break;
		case CHANID_TRIANGLE:
		case CHANID_NOISE:
		case CHANID_DPCM:
			MixInternal2(Time);
			break;
		default:
			if (ChanID >= CHANID_N163_CHAN1 && ChanID <= CHANID_N163_CHAN8 && ChanID == m_iN163Chan)
				MixN163Sides(Time);
			// The VRC7 makes its samples at the end of the frame, for where each channel goes then
			break;
	}
}

int CMixer::SplitPin(Blip_Synth<blip_good_quality, -500> &Synth, const int *pChans, const double *pWeighted, int Count, double Sum,
	int Side_, int Time, bool Send)
{
	// Sum is what the pin puts out (with the mixer window's levels); each channel's share of it is in proportion to
	// its input times its level, as the levels scale it. A channel in its own buffers has its share there, in whole
	// steps of the synth; what it gets is taken out of the sides' step, so the two add up to the pin's steps as
	// they were. Returns what the channels' own buffers got.
	double Total = 0.0;
	for (int i = 0; i < Count; ++i)
		Total += pWeighted[i];
	int Moved = 0;
	for (int i = 0; i < Count; ++i) {
		const int c = pChans[i];
		const int32 Want = m_bOwn[c] && Total > 0.0 ? static_cast<int32>(std::floor(Sum * pWeighted[i] / Total * AMP_2A03 + 0.5)) : 0;
		if (Want != m_iPinOwn[c][Side_]) {
			if (Send)
				Synth.offset(Time, Want - m_iPinOwn[c][Side_], &m_OwnBuffer[c][Side_]);
			Moved += Want - m_iPinOwn[c][Side_];
			m_iPinOwn[c][Side_] = Want;
		}
	}
	return Moved;
}

int CMixer::ReadDepth(int Size, blip_sample_t *pOut, bool Stereo)
{
	// The sides, then each channel in its own buffers added, softened and sent to the space, then the space
	long Count = std::min(static_cast<long>(Size), BlipBuffer.samples_avail());
	Count = std::max(0L, std::min(Count, m_iDepthSize));
	float *const pLeft = m_pDepth[0], *const pRight = m_pDepth[1], *const pSendLeft = m_pDepth[2], *const pSendRight = m_pDepth[3];
	if (pLeft == NULL || m_pDepthRead == NULL)
		return 0;
	blip_sample_t *const pReadLeft = m_pDepthRead, *const pReadRight = m_pDepthRead + m_iDepthSize;
	BlipBuffer.read_samples(pReadLeft, Count);
	BlipBufferRight.read_samples(pReadRight, Count);
	for (long i = 0; i < Count; ++i) {
		pLeft[i] = pReadLeft[i];
		pRight[i] = pReadRight[i];
		pSendLeft[i] = pSendRight[i] = 0.0f;
	}
	bool Changed = false;
	for (int c = 0; c < CHANNELS; ++c) {
		if (!OwnActive(c))
			continue;
		const long GotLeft = m_OwnBuffer[c][0].read_samples(pReadLeft, Count);
		const long GotRight = m_OwnBuffer[c][1].read_samples(pReadRight, Count);
		for (long i = GotLeft; i < Count; ++i)
			pReadLeft[i] = 0;
		for (long i = GotRight; i < Count; ++i)
			pReadRight[i] = 0;
		CSoftFilter &Filter = m_SoftFilter[c];
		const bool Soft = Filter.Soft() > 0;
		const float Send = m_bOwn[c] ? m_iSpaceOf[c] / 100.0f : 0.0f;
		for (long i = 0; i < Count; ++i) {
			float l = pReadLeft[i], r = pReadRight[i];
			if (Soft)
				Filter.Run(l, r);
			pLeft[i] += l;
			pRight[i] += r;
			if (Send > 0.0f) {
				pSendLeft[i] += Send * l;
				pSendRight[i] += Send * r;
			}
		}
		if (!m_bOwn[c] && --m_iOwnDrain[c] == 0) {
			Filter.Clear();
			Changed = true;
		}
	}
	if (Changed)
		CountOwn();
	m_Space.Process(pSendLeft, pSendRight, pLeft, pRight, static_cast<int>(Count));

	for (long i = 0; i < Count; ++i) {
		const float l = pLeft[i], r = pRight[i];
		const int Left = std::max(-32768, std::min(32767, static_cast<int>(l < 0.0f ? l - 0.5f : l + 0.5f)));
		const int Right = std::max(-32768, std::min(32767, static_cast<int>(r < 0.0f ? r - 0.5f : r + 0.5f)));
		if (Stereo) {
			pOut[i * 2] = static_cast<blip_sample_t>(Left);
			pOut[i * 2 + 1] = static_cast<blip_sample_t>(Right);
		}
		else
			pOut[i] = static_cast<blip_sample_t>((Left + Right) / 2);
	}
	return static_cast<int>(Count);
}

// Stems

void CMixer::FreeStems()
{
	for (int k = 0; k < m_iStemCount; ++k) {
		delete m_pStem[k];
		m_pStem[k] = NULL;
	}
	m_iStemCount = 0;
}

void CMixer::SetStems(int Count, const bool (*pSilent)[CHANNELS], int Time)
{
	FreeStems();
	Count = pSilent != NULL ? std::max(0, std::min(static_cast<int>(MAX_STEMS), Count)) : 0;
	for (int k = 0; k < Count; ++k) {
		CMixer *pStem = new CMixer();
		pStem->m_bStem = true;
		pStem->MakeStemOf(*this);
		// Its channels left out as CSoundGen leaves them out of a render of some parts alone (ApplyChannelMix): one at
		// a time, the 2A03's off their pins and the others' at a level of nothing
		for (int i = 0; i < CHANNELS; ++i) {
			m_bStemSilent[k][i] = pSilent[k][i];
			if (!pSilent[k][i])
				continue;
			if (i <= CHANID_DPCM)
				pStem->SetChannelSilent(i, true, Time);
			else
				pStem->SetChannelGain(i, 0.0, Time);
		}
		m_pStem[k] = pStem;
	}
	m_iStemCount = Count;
}

void CMixer::MakeStemOf(const CMixer &M)
{
	// The settings
	m_fLevelAPU1 = M.m_fLevelAPU1;
	m_fLevelAPU2 = M.m_fLevelAPU2;
	m_fLevelVRC6 = M.m_fLevelVRC6;
	m_fLevelMMC5 = M.m_fLevelMMC5;
	m_fLevelFDS = M.m_fLevelFDS;
	m_fLevelN163 = M.m_fLevelN163;
	m_fLevelS5B = M.m_fLevelS5B;
	m_iExternalChip = M.m_iExternalChip;
	m_iLowCut = M.m_iLowCut;
	m_iHighCut = M.m_iHighCut;
	m_iHighDamp = M.m_iHighDamp;
	m_fOverallVol = M.m_fOverallVol;
	m_fVolumeVRC6 = M.m_fVolumeVRC6;
	m_fVolumeMMC5 = M.m_fVolumeMMC5;

	// The channels, as they are
	for (int i = 0; i < CHANNELS; ++i) {
		m_iChannels[i] = M.m_iChannels[i];
		m_iPan[i] = M.m_iPan[i];
		m_dLevel[i] = M.m_dLevel[i];
		m_bSilent[i] = M.m_bSilent[i];
		m_iLevel[i] = M.m_iLevel[i];
		m_bOwn[i] = M.m_bOwn[i];
		m_iOwnDrain[i] = M.m_iOwnDrain[i];
		m_iSoftOf[i] = M.m_iSoftOf[i];
		m_iSpaceOf[i] = M.m_iSpaceOf[i];
		for (int s = 0; s < 2; ++s) {
			m_dGain[i][s] = M.m_dGain[i][s];
			m_dSide[i][s] = M.m_dSide[i][s];
			m_iSent[i][s] = M.m_iSent[i][s];
			m_iOwnSent[i][s] = M.m_iOwnSent[i][s];
			m_iPinOwn[i][s] = M.m_iPinOwn[i][s];
		}
	}
	m_iN163Level = M.m_iN163Level;
	m_iN163Chan = M.m_iN163Chan;
	m_iN163Dest = M.m_iN163Dest;
	for (int s = 0; s < 2; ++s) {
		m_iN163Sent[s] = M.m_iN163Sent[s];
		m_dSumSS[s] = M.m_dSumSS[s];
		m_dSumTND[s] = M.m_dSumTND[s];
	}

	// Its buffers, empty, at the same rates and with the same low cut: the clock rate first, which the buffers' sample
	// rates take in (the own buffers get the low cut from AllocateBuffer)
	SetClockRate(M.BlipBuffer.clock_rate());
	AllocateBuffer(M.m_iBufferLength, M.m_iSampleRate, M.m_iNrChannels);
	BlipBuffer.bass_freq(m_iLowCut);
	BlipBufferRight.bass_freq(m_iLowCut);

	// The synths exactly as they are: a small volume given once attenuates a kernel for good (Blip_Synth_::volume_unit)
	Synth2A03SS.copy_state(M.Synth2A03SS);
	Synth2A03TND.copy_state(M.Synth2A03TND);
	SynthVRC6.copy_state(M.SynthVRC6);
	SynthMMC5.copy_state(M.SynthMMC5);
	SynthN163.copy_state(M.SynthN163);
	SynthFDS.copy_state(M.SynthFDS);
	SynthS5B.copy_state(M.SynthS5B);
	for (int i = 0; i < 3; ++i) {
		SynthVRC6Own[i].copy_state(M.SynthVRC6Own[i]);
		SynthMMC5Own[i].copy_state(M.SynthMMC5Own[i]);
	}

	// Soft as it is, and the space with its settings, silent
	for (int i = 0; i < CHANNELS; ++i)
		m_SoftFilter[i] = M.m_SoftFilter[i];
	m_Space.Set(M.m_Space.Room(), M.m_Space.EchoSeconds(), M.m_Space.Repeats());
	m_Space.Clear();
	CountOwn();
}

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

#ifndef MIXER_H
#define MIXER_H

#include "Types.h"
#include "../Common.h"
#include "../Blip_Buffer/blip_buffer.h"

enum chip_level_t {
	CHIP_LEVEL_APU1,
	CHIP_LEVEL_APU2,
	CHIP_LEVEL_VRC6,
	CHIP_LEVEL_VRC7,
	CHIP_LEVEL_MMC5,
	CHIP_LEVEL_FDS,
	CHIP_LEVEL_N163,
	CHIP_LEVEL_S5B
};

class CMixer
{
public:
	CMixer();
	~CMixer();

	void	ExternalSound(int Chip);
	void	AddValue(int ChanID, int Chip, int Value, int AbsValue, int FrameCycles);
	void	UpdateSettings(int LowCut,	int HighCut, int HighDamp, float OverallVol);

	bool	AllocateBuffer(unsigned int Size, uint32 SampleRate, uint8 NrChannels);
	void	SetClockRate(uint32 Rate);
	void	ClearBuffer();
	int		FinishBuffer(int t);
	int		SamplesAvail() const;
	void	MixSamples(blip_sample_t *pBuffer, uint32 Count);
	uint32	GetMixSampleCount(int t) const;

	void	AddSample(int ChanID, int Value);
	int		ReadBuffer(int Size, void *Buffer, bool Stereo);

	// Stereo (this FamiTracker's own, the NES being mono): a channel's pan,
	// 0x00 left, 0x80 the middle (where every channel starts), 0xFF right,
	// from Time (cycles into the frame) on
	void	SetChannelPan(int ChanID, int Pan, int Time);
	int		GetChannelPan(int ChanID) const;
	// What a channel's level is multiplied by on each side, for the chips that
	// make their samples themselves (VRC7, 5B): 256 is the whole of it
	void	PanGains256(int ChanID, int32 &Left, int32 &Right) const;
	// The pan law: the side a sound is panned away from fades out, the other
	// stays whole, so a sound in the middle is on both sides as the mono mix had it
	static void PanGains(int Pan, double &Left, double &Right);
	// For the VRC7 and the 5B: their samples for each side
	void	MixSamples(blip_sample_t *pLeft, blip_sample_t *pRight, uint32 Count);
	// The N163's DAC plays the sample of another channel, at the level the last one had
	void	N163Channel(int ChanID, int Time);
	// The mixer window's level for a channel (this FamiTracker's own, View > Mixer): its sound
	// multiplied by Gain from Time on, 1 as it plays. On the 2A03 it scales the channel's share
	// of the output its two pins mix, so the chip compresses as before.
	void	SetChannelGain(int ChanID, double Gain, int Time);
	double	GetChannelGain(int ChanID) const;

	int32	GetChanOutput(uint8 Chan) const;
	void	SetChipLevel(chip_level_t Chip, float Level);
	uint32	ResampleDuration(uint32 Time) const;
	void	SetNamcoVolume(float fVol);

private:
	inline double CalcPin1(double Val1, double Val2);
	inline double CalcPin2(double Val1, double Val2, double Val3);

	void MixInternal1(int Time);
	void MixInternal2(int Time);
	void MixN163(int ChanID, int Value, int Time);
	void MixFDS(int ChanID, int Value, int Time);
	void MixVRC6(int ChanID, int Value, int Time);
	void MixMMC5(int ChanID, int Value, int Time);
	void MixS5B(int ChanID, int Value, int Time);
	void MixN163Sides(int Time);
	void Remix(int ChanID, int Time);	// a channel's pan or level changed: its sound to where it is now
	template <class Synth> void MixSides(Synth &Synth_, int32 Level, const double *pGain, int32 *pSent, int Time);
	Blip_Buffer &Side(int Index) { return Index == 0 ? BlipBuffer : BlipBufferRight; }

	void StoreChannelLevel(int Channel, int Value);
	void ClearChannelLevels();

	float GetAttenuation() const;

private:
	// Blip buffer synths
	Blip_Synth<blip_good_quality, -500>		Synth2A03SS;
	Blip_Synth<blip_good_quality, -500>		Synth2A03TND;
	Blip_Synth<blip_good_quality, -500>		SynthVRC6;
	Blip_Synth<blip_good_quality, -130>		SynthMMC5;	
	Blip_Synth<blip_good_quality, -1600>	SynthN163;
	Blip_Synth<blip_good_quality, -3500>	SynthFDS;
	Blip_Synth<blip_good_quality, -2000>	SynthS5B;
	
	// Blip buffer objects: the left side, and the right
	Blip_Buffer	BlipBuffer;
	Blip_Buffer	BlipBufferRight;

	double		m_dSumSS[2];				// the 2A03's two outputs on each side
	double		m_dSumTND[2];

	int32		m_iChannels[CHANNELS];

	// Stereo
	int			m_iPan[CHANNELS];			// 0x00-0xFF
	double		m_dGain[CHANNELS][2];		// left and right, from the pan
	double		m_dLevel[CHANNELS];			// the mixer window's level (1 as the channel plays)
	double		m_dSide[CHANNELS][2];		// the pan's gains and the level together, for the chips mixed linearly
	int32		m_iLevel[CHANNELS];			// the linear chips' channels: the level the deltas sent add up to
	int32		m_iSent[CHANNELS][2];		// ...and what each side has of it
	int32		m_iN163Level;				// the N163's DAC: its level, the channel it plays and what each side has of it
	int			m_iN163Chan;
	int32		m_iN163Sent[2];
	uint8		m_iExternalChip;
	uint32		m_iSampleRate;

	float		m_fChannelLevels[CHANNELS];
	uint32		m_iChanLevelFallOff[CHANNELS];

	int			m_iLowCut;
	int			m_iHighCut;
	int			m_iHighDamp;
	float		m_fOverallVol;

	float		m_fLevelAPU1;
	float		m_fLevelAPU2;
	float		m_fLevelVRC6;
	float		m_fLevelMMC5;
	float		m_fLevelFDS;
	float		m_fLevelN163;
	float		m_fLevelS5B;
};

#endif /* MIXER_H */

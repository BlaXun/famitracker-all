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
#include "Space.h"

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
	// A channel of the 2A03's second pin at Value as if it had always been there (this FamiTracker's own): the
	// pin's output taken in without a step, as a song starts with the DPCM level its first row sets
	void	StartAt(int ChanID, int Value);

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
	// Depth (this FamiTracker's own, View > Mixer; Space.h): a channel's Soft (0 off to 100) and its send to the
	// space (0-100), from Time on. A channel with either on is mixed into Blip_Buffers of its own, which
	// ReadBuffer softens and sends before adding them to the sides; with every channel's at 0 nothing changes.
	void	SetChannelDepth(int ChanID, int Soft, int Space, int Time);
	// The space all channels share: its room (CSpace's numbers), the echo's time in seconds (0: no echo) and
	// its repeats
	void	SetSpace(int Room, double EchoSeconds, int Repeats);
	bool	SpaceRinging() const;				// the space's sound is still dying away
	void	ClearSpace();						// the space silent (ClearBuffer leaves it ringing), as a song starts
	bool	IsOwn(int ChanID) const;			// mixed into buffers of its own (the VRC7 makes its samples itself)
	void	MixOwnSamples(int ChanID, blip_sample_t *pLeft, blip_sample_t *pRight, uint32 Count);

	int32	GetChanOutput(uint8 Chan) const;
	void	SetChipLevel(chip_level_t Chip, float Level);
	uint32	ResampleDuration(uint32 Time) const;
	void	SetNamcoVolume(float fVol);

private:
	inline double CalcPin1(double Val1, double Val2);
	inline double CalcPin2(double Val1, double Val2, double Val3);

	void MixInternal1(int Time);
	void MixInternal2(int Time, bool Send = true);	// Send false: the pin's output taken in, nothing sent (StartAt)
	void MixN163(int ChanID, int Value, int Time);
	void MixFDS(int ChanID, int Value, int Time);
	void MixVRC6(int ChanID, int Value, int Time);
	void MixMMC5(int ChanID, int Value, int Time);
	void MixS5B(int ChanID, int Value, int Time);
	void MixN163Sides(int Time);
	void Remix(int ChanID, int Time);	// a channel's pan or level changed: its sound to where it is now
	// Dest: where a channel's sound goes, -1 for the sides, or the channel whose own buffers have it (Depth)
	template <class Synth> void MixSides(Synth &Synth_, int Dest, int32 Level, const double *pGain, int32 *pSent, int Time);
	template <class Synth> void MixSides(Synth &Synth_, Synth &Own, bool Faded, int Dest, int32 Level, const double *pGain, int32 *pSent,
		int32 *pOwnSent, int Time);
	// The 2A03's pins with a channel in its own buffers: each such channel gets its share of the pin, the sides the rest
	int SplitPin(Blip_Synth<blip_good_quality, -500> &Synth, const int *pChans, const double *pWeighted, int Count, double Sum,
		int Side_, int Time, bool Send = true);
	int DestOf(int ChanID) const { return m_bOwn[ChanID] ? ChanID : -1; }
	Blip_Buffer &Buffer(int Dest, int Index) { return Dest < 0 ? Side(Index) : m_OwnBuffer[Dest][Index]; }
	template <class Synth> void Move(Synth &Synth_, int32 Amount, int From, int To, int Index, int Time);
	void MoveOwn(int ChanID, int Time);		// the channel's sound now goes to its own buffers, or back to the sides
	bool OwnActive(int ChanID) const { return m_bOwn[ChanID] || m_iOwnDrain[ChanID] > 0; }
	void CountOwn();
	int ReadDepth(int Size, blip_sample_t *pOut, bool Stereo);
	void FreeDepth();
	void SetOwnVolumes();					// the channels' own synths at their chips' volumes times their levels
	void ClearOwn(int ChanID, int Time);	// what a channel's own synth has, taken out (before its volume changes)
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

	// The mixer window's levels on the VRC6 and the MMC5, whose channels' levels are small whole numbers
	// (0-15): a channel off 0 dB sounds through a synth of its own at its chip's volume times its level
	// (whole steps of the level itself would round the fader away at low volumes); at 0 dB through the
	// chip's, as it always did. The other chips' levels are fine enough to scale (m_dSide), and the 5B's
	// and the N163's synths count too coarsely for a gain of their own.
	Blip_Synth<blip_good_quality, -500>		SynthVRC6Own[3];
	Blip_Synth<blip_good_quality, -130>		SynthMMC5Own[3];
	float		m_fVolumeVRC6, m_fVolumeMMC5;	// the chips' synths' volumes
	
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
	double		m_dSide[CHANNELS][2];		// the pan's gains and the level together, for the FDS, N163, 5B and VRC7
	int32		m_iLevel[CHANNELS];			// the linear chips' channels: the level the deltas sent add up to
	int32		m_iSent[CHANNELS][2];		// ...and what each side has of it
	int32		m_iN163Level;				// the N163's DAC: its level, the channel it plays and what each side has of it
	int			m_iN163Chan;
	int32		m_iN163Sent[2];
	int32		m_iOwnSent[CHANNELS][2];	// what a channel's own synth has of its level on each side

	// Depth: the channels with Soft or Space on, each mixed into its own two buffers (in step with the sides',
	// read for a second after it left them, while what it had dies away), and the space they send to
	Blip_Buffer	m_OwnBuffer[CHANNELS][2];
	bool		m_bOwn[CHANNELS];
	int			m_iOwnDrain[CHANNELS];		// frames it's still read after it left them
	int			m_iOwnCount;				// channels in their own buffers or still read
	int			m_iSoftOf[CHANNELS];
	int			m_iSpaceOf[CHANNELS];
	CSoftFilter	m_SoftFilter[CHANNELS];
	int32		m_iPinOwn[CHANNELS][2];		// the 2A03's channels in their own buffers: their share of the pin there
	int			m_iN163Dest;				// where the N163's DAC level is (Dest)
	CSpace		m_Space;
	float		*m_pDepth[4];				// for ReadDepth: the two sides, the two sends
	blip_sample_t *m_pDepthRead;
	long		m_iDepthSize;
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

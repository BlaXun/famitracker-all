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

#ifndef VRC7_H
#define VRC7_H

#include "external.h"
#include "emu2413.h"

class CVRC7 : public CExternal {
public:
	CVRC7(CMixer *pMixer);
	virtual ~CVRC7();

	void Reset();
	void SetSampleSpeed(uint32 SampleRate, double ClockRate, uint32 FrameRate);
	void SetVolume(float Volume);
	void Write(uint16 Address, uint8 Value);
	uint8 Read(uint16 Address, bool &Mapped);
	void EndFrame();
	void Process(uint32 Time);
	// Stems (CMixer::SetStems, this FamiTracker's own): each one's samples, made from the channels' values as the mix's
	// are, with the levels and pans its mixer has; Count 0 for none
	void SetStems(int Count);

protected:
	static const float  AMPLIFY;
	static const uint32 OPL_CLOCK;

private:
	OPLL	*m_pOPLLInt;
	uint32	m_iTime;
	uint32	m_iMaxSamples;

	int16	*m_pBuffer;
	int16	*m_pBufferRight;	// the right side (m_pBuffer is the left)
	int16	*m_pOwn[6][2];		// the channels the mixer has in buffers of their own (Depth): each one's sides
	int32	m_iOwnLast[6][2];
	uint32	m_iBufferPtr;
	int32	m_iLastSample[2];	// each side's last sample (a sample is the average with the one before)

	// Stems: for each, its two sides and each channel's own two (as m_pOwn), m_iMaxSamples samples each, and the last
	// sample of each of them
	static const int STEM_BUFFERS = 14;
	int		m_iStems;
	int16	*m_pStemSamples;
	int32	*m_pStemLast;
	int16	*StemBuffer(int Stem, int Buffer) const { return m_pStemSamples + (Stem * STEM_BUFFERS + Buffer) * m_iMaxSamples; }
	int32	&StemLast(int Stem, int Buffer) const { return m_pStemLast[Stem * STEM_BUFFERS + Buffer]; }
	int16	Finished(int32 RawSample, int32 &Last) const;	// clipped, at the volume, the average with the one before
	void	FreeStems();

	uint8	m_iSoundReg;

	float	m_fVolume;
};


#endif /* VRC7_H */

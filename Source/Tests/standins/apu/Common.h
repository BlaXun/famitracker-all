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

// Stand-in for Common.h: the same, with types of the same sizes as on
// Windows (where long is 32 bits), no Microsoft keywords and the standard
// headers the Windows headers bring along, so the sound chips' emulation
// builds on Linux (see CMakeLists.txt)
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>

typedef uint8_t		uint8;
typedef uint16_t	uint16;
typedef uint32_t	uint32;
typedef uint64_t	uint64;
typedef int8_t		int8;
typedef int16_t		int16;
typedef int32_t		int32;
typedef int64_t		int64;

#define __fastcall

#define _MAIN_H_

#define SAMPLES_IN_BYTES(x) (x << SampleSizeShift)

const int SPEED_AUTO	= 0;
const int SPEED_NTSC	= 1;
const int SPEED_PAL		= 2;


// Used to get the DPCM state
struct stDPCMState {
	int SamplePos;
	int DeltaCntr;
};

// Used to play the audio when the buffer is full
class IAudioCallback {
public:
	virtual void FlushBuffer(int16 *Buffer, uint32 Size) = 0;
};


// class for simulating CPU memory, used by the DPCM channel
class CSampleMem
{
public:
	CSampleMem() : m_pMemory(0), m_iMemSize(0) {
	};

	uint8 Read(uint16 Address) const {
		if (!m_pMemory)
			return 0;
		uint16 Addr = (Address - 0xC000);// % m_iMemSize;
		if (Addr >= m_iMemSize)
			return 0;
		return m_pMemory[Addr];
	};

	void SetMem(const char *pPtr, int Size) {
		m_pMemory = (uint8*)pPtr;
		m_iMemSize = Size;
	};

	void Clear() {
		m_pMemory = 0;
		m_iMemSize = 0;
	}

private:
	const uint8 *m_pMemory;
	uint16 m_iMemSize;
};

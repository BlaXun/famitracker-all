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

#ifndef S5B_H
#define S5B_H

#include "external.h"
#include "channel.h"

// The Sunsoft 5B, a YM2149 clocked by the CPU with its divider on (see S5B.cpp)
class CS5B : public CExternal {
public:
	CS5B(CMixer *pMixer);
	virtual ~CS5B();
	void	Reset();
	void	Process(uint32 Time);
	void	EndFrame();
	void	Write(uint16 Address, uint8 Value);
	uint8 	Read(uint16 Address, bool &Mapped);

private:
	void	MixLevels();

private:
	uint8	m_iRegister;
	uint32	m_iTime;			// cycles into the audio frame
	uint32	m_iTickCycles;		// cycles until the chip's next tick
	int32	m_iLevels[3];		// each channel's level, as the mixer has it
};

#endif /* S5B_H */

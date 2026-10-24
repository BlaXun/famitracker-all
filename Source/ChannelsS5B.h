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

#pragma once

//
// Derived channels, 5B
//
// The Sunsoft 5B's three square channels. They share the chip's noise
// generator and its envelope generator, whose registers the third channel
// writes after its own (CSoundGen refreshes the channels in chan_id_t order).
// The effects play as in 0CC-FamiTracker and Dn-FamiTracker, so modules play
// the same there: Hxy starts the envelope with shape y (and its period follows
// the note when x isn't 0), Ixx and Jxx set the period's high and low bytes,
// Vxx turns the square (1), the noise (2) and the envelope (4) on. A 5B
// instrument's "noise / mode" sequence does the same a frame at a time
// (S5B_MODE_* in Sequence.h, the noise pitch in bits 0-4).
//

class CChannelHandlerS5B : public CChannelHandler {
public:
	CChannelHandlerS5B();
	virtual void ProcessChannel();
	virtual void RefreshChannel();
	virtual void ResetChannel();

	// The envelope period that follows a tone period, as Hxy's x asks: 8 is the
	// same period, each step above halves it (rounded) and each step below doubles
	// it. 12 makes a saw shape sound the note (the envelope ramps 32 steps of 16
	// clocks, the square toggles every 16 clocks times its period).
	static int AutoEnvelopePeriod(int Period, int Shift);

protected:
	virtual void HandleNoteData(stChanNote *pNoteData, int EffColumns);
	virtual void HandleCustomEffects(int EffNum, int EffParam);
	virtual bool HandleInstrument(int Instrument, bool Trigger, bool NewInstrument);
	virtual void HandleEmptyNote();
	virtual void HandleCut();
	virtual void HandleRelease();
	virtual void HandleNote(int Note, int Octave);
	virtual void ClearRegisters();
	virtual int  CalculateVolume() const;
	virtual void SetDutyPeriod(int Period);

private:
	int  ChipChannel() const;		// 0-2
	void WriteReg(int Reg, int Value);
	void WriteSharedRegs();

	// The channel's
	int  m_iMode;				// the S5B_MODE_* bits playing
	int  m_iDefaultMode;		// what notes start with (Vxx)
	int  m_iAutoShift;			// Hxy's x: 0 when the envelope's period doesn't follow the note
	bool m_bNoteStarted;		// a note started since the last refresh
	bool m_bEnvelopeStarted;	// Hxy since the last refresh
	int  m_iPostEffect;
	int  m_iPostEffectParam;

	// The chip's, which its three channels share
	static int  s_iMixer;			// $07: a bit set turns a channel's square or noise off
	static int  s_iNoisePitch;		// 0-31, low to high: $06 gets 31 minus it
	static int  s_iNoiseWritten;
	static int  s_iEnvelopePeriod;	// $0B and $0C
	static int  s_iEnvelopeShape;	// $0D
	static bool s_bEnvelopeRestart;	// write $0D, which restarts the envelope
	static bool s_bSongEnvelope;	// the song set the envelope (Hxy, Ixx or Jxx) since the reset; until
									// it does, notes with the envelope play a falling saw at their pitch
};

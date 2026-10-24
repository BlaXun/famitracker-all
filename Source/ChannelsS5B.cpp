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

// Sunsoft 5B (YM2149): see ChannelsS5B.h

#include <algorithm>
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "ChannelHandler.h"
#include "ChannelsS5B.h"
#include "SoundGen.h"
#include "APU/Types.h"

namespace {

// Until the song sets the envelope: a falling saw that repeats, its period
// following the note so that it sounds the note (as HC8 does)
const int DEFAULT_ENVELOPE_SHAPE = 0x08;
const int DEFAULT_AUTO_SHIFT = 0x0C;

const int ALL_MODES = S5B_MODE_ENVELOPE | S5B_MODE_SQUARE | S5B_MODE_NOISE;

}	// namespace

int  CChannelHandlerS5B::s_iMixer = 0x3F;
int  CChannelHandlerS5B::s_iNoisePitch = 0;
int  CChannelHandlerS5B::s_iNoiseWritten = -1;
int  CChannelHandlerS5B::s_iEnvelopePeriod = 0;
int  CChannelHandlerS5B::s_iEnvelopeShape = 0;
bool CChannelHandlerS5B::s_bEnvelopeRestart = false;
bool CChannelHandlerS5B::s_bSongEnvelope = false;

CChannelHandlerS5B::CChannelHandlerS5B() :
	CChannelHandler(0xFFF, 0x0F),
	m_iMode(S5B_MODE_SQUARE),
	m_iDefaultMode(S5B_MODE_SQUARE),
	m_iAutoShift(0),
	m_bNoteStarted(false),
	m_bEnvelopeStarted(false),
	m_iPostEffect(0),
	m_iPostEffectParam(0)
{
}

int CChannelHandlerS5B::AutoEnvelopePeriod(int Period, int Shift)
{
	if (Shift > 8) {
		const int Steps = Shift - 8;
		return (Period + (1 << (Steps - 1))) >> Steps;
	}
	if (Shift < 8)
		return std::min(Period << (8 - Shift), 0xFFFF);
	return Period;
}

int CChannelHandlerS5B::ChipChannel() const
{
	return m_iChannelID - CHANID_S5B_CH1;
}

void CChannelHandlerS5B::WriteReg(int Reg, int Value)
{
	WriteExternalRegister(0xC000, static_cast<uint8>(Reg));
	WriteExternalRegister(0xE000, static_cast<uint8>(Value));
}

void CChannelHandlerS5B::HandleNoteData(stChanNote *pNoteData, int EffColumns)
{
	m_iPostEffect = 0;
	m_iPostEffectParam = 0;

	CChannelHandler::HandleNoteData(pNoteData, EffColumns);

	if (pNoteData->Note != NONE && pNoteData->Note != HALT && pNoteData->Note != RELEASE) {
		if (m_iPostEffect && (m_iEffect == EF_SLIDE_UP || m_iEffect == EF_SLIDE_DOWN))
			SetupSlide(m_iPostEffect, m_iPostEffectParam);
		else if (m_iEffect == EF_SLIDE_DOWN || m_iEffect == EF_SLIDE_UP)
			m_iEffect = EF_NONE;
	}
}

void CChannelHandlerS5B::HandleCustomEffects(int EffNum, int EffParam)
{
	if (CheckCommonEffects(EffNum, EffParam))
		return;

	switch (EffNum) {
		case EF_DUTY_CYCLE:
			// Vxx: 1 the square, 2 the noise, 4 the envelope
			m_iDefaultMode = m_iMode = ((EffParam & 1) ? S5B_MODE_SQUARE : 0) | ((EffParam & 2) ? S5B_MODE_NOISE : 0) |
				((EffParam & 4) ? S5B_MODE_ENVELOPE : 0);
			break;
		case EF_SUNSOFT_ENV_TYPE:
			s_iEnvelopeShape = EffParam & 0x0F;
			m_iAutoShift = EffParam >> 4;
			m_bEnvelopeStarted = true;
			s_bSongEnvelope = true;
			break;
		case EF_SUNSOFT_ENV_HI:
			s_iEnvelopePeriod = (s_iEnvelopePeriod & 0x00FF) | (EffParam << 8);
			s_bSongEnvelope = true;
			break;
		case EF_SUNSOFT_ENV_LO:
			s_iEnvelopePeriod = (s_iEnvelopePeriod & 0xFF00) | EffParam;
			s_bSongEnvelope = true;
			break;
		case EF_SLIDE_UP:
		case EF_SLIDE_DOWN:
			m_iPostEffect = EffNum;
			m_iPostEffectParam = EffParam;
			SetupSlide(EffNum, EffParam);
			break;
	}
}

bool CChannelHandlerS5B::HandleInstrument(int Instrument, bool Trigger, bool NewInstrument)
{
	CFamiTrackerDoc *pDocument = m_pSoundGen->GetDocument();
	CInstrumentContainer<CInstrumentS5B> instContainer(pDocument, Instrument);
	CInstrumentS5B *pInstrument = instContainer();

	if (pInstrument == NULL)
		return false;

	for (int i = 0; i < CInstrumentS5B::SEQUENCE_COUNT; ++i) {
		const CSequence *pSequence = pDocument->GetSequence(SNDCHIP_S5B, pInstrument->GetSeqIndex(i), i);
		if (Trigger || !IsSequenceEqual(i, pSequence) || pInstrument->GetSeqEnable(i) > GetSequenceState(i)) {
			if (pInstrument->GetSeqEnable(i) == 1)
				SetupSequence(i, pSequence);
			else
				ClearSequence(i);
		}
	}

	return true;
}

void CChannelHandlerS5B::HandleEmptyNote()
{
}

void CChannelHandlerS5B::HandleCut()
{
	CutNote();
}

void CChannelHandlerS5B::HandleRelease()
{
	if (!m_bRelease) {
		ReleaseNote();
		ReleaseSequences();
	}
}

void CChannelHandlerS5B::HandleNote(int Note, int Octave)
{
	m_iNote = RunNote(Octave, Note);
	m_iSeqVolume = 0x0F;
	m_iMode = m_iDefaultMode;
	m_bNoteStarted = true;
}

void CChannelHandlerS5B::SetDutyPeriod(int Period)
{
	// A frame of the instrument's noise / mode sequence
	m_iMode = Period & ALL_MODES;
	if (Period & S5B_MODE_NOISE)
		s_iNoisePitch = Period & 0x1F;
}

int CChannelHandlerS5B::CalculateVolume() const
{
	// The 5B's volume steps are 3 dB each, so the volume column and the
	// instrument's volume add: each step under 15 takes 3 dB more off
	if (!m_bGate)
		return 0;
	return LimitVolume((m_iVolume >> VOL_COLUMN_SHIFT) + m_iSeqVolume - 15 - GetTremolo());
}

void CChannelHandlerS5B::ProcessChannel()
{
	// Default effects
	CChannelHandler::ProcessChannel();

	// Sequences
	for (int i = 0; i < CInstrumentS5B::SEQUENCE_COUNT; ++i)
		RunSequence(i);
}

void CChannelHandlerS5B::RefreshChannel()
{
	const int Channel = ChipChannel();
	const int Period = CalculatePeriod();
	const int Volume = CalculateVolume();

	// A note at volume 0 is silent, also with the envelope (which sets the level itself)
	const bool On = m_bGate && Volume > 0;
	const bool Envelope = On && (m_iMode & S5B_MODE_ENVELOPE) != 0;
	const bool Square = On && (m_iMode & S5B_MODE_SQUARE) != 0;
	const bool Noise = On && (m_iMode & S5B_MODE_NOISE) != 0;

	const int SquareOff = 1 << Channel, NoiseOff = 8 << Channel;
	s_iMixer |= SquareOff | NoiseOff;
	if (Square)
		s_iMixer &= ~SquareOff;
	if (Noise)
		s_iMixer &= ~NoiseOff;

	WriteReg(Channel * 2, Period & 0xFF);
	WriteReg(Channel * 2 + 1, Period >> 8);
	WriteReg(8 + Channel, On ? (Envelope ? 0x10 | Volume : Volume) : 0);

	if (Envelope) {
		// Each new note starts the envelope again, its period following the note when asked
		if (m_bNoteStarted)
			s_bEnvelopeRestart = true;
		const int Shift = s_bSongEnvelope ? m_iAutoShift : DEFAULT_AUTO_SHIFT;
		if (Shift != 0)
			s_iEnvelopePeriod = AutoEnvelopePeriod(Period, Shift);
	}
	if (m_bEnvelopeStarted)
		s_bEnvelopeRestart = true;
	m_bNoteStarted = false;
	m_bEnvelopeStarted = false;

	if (m_iChannelID == CHANID_S5B_CH3)
		WriteSharedRegs();
}

void CChannelHandlerS5B::WriteSharedRegs()
{
	if (s_iNoisePitch != s_iNoiseWritten) {
		WriteReg(0x06, 0x1F - s_iNoisePitch);
		s_iNoiseWritten = s_iNoisePitch;
	}
	WriteReg(0x07, s_iMixer & 0x3F);
	WriteReg(0x0B, s_iEnvelopePeriod & 0xFF);
	WriteReg(0x0C, s_iEnvelopePeriod >> 8);
	if (s_bEnvelopeRestart) {
		WriteReg(0x0D, s_bSongEnvelope ? s_iEnvelopeShape : DEFAULT_ENVELOPE_SHAPE);
		s_bEnvelopeRestart = false;
	}
}

void CChannelHandlerS5B::ResetChannel()
{
	CChannelHandler::ResetChannel();

	m_iMode = m_iDefaultMode = S5B_MODE_SQUARE;
	m_iAutoShift = 0;
	m_bNoteStarted = false;
	m_bEnvelopeStarted = false;
	m_iPostEffect = 0;
	m_iPostEffectParam = 0;

	// The chip's (each of its channels resets it)
	s_iMixer = 0x3F;
	s_iNoisePitch = 0;
	s_iNoiseWritten = -1;
	s_iEnvelopePeriod = 0;
	s_iEnvelopeShape = 0;
	s_bEnvelopeRestart = false;
	s_bSongEnvelope = false;
}

void CChannelHandlerS5B::ClearRegisters()
{
	WriteReg(8 + ChipChannel(), 0);		// silent
}

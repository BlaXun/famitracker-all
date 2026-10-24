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

// Soft and Space (see Space.h)

#include "Space.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

const double PI = 3.14159265358979323846;
const double SOFT_TOP = 12000.0, SOFT_BOTTOM = 1800.0;	// Hz, at Soft 1 and 100

// The rooms, in ftapi::SPACE_ROOM_NAMES's order. Size scales the lines (at 1, 23 to 48 ms), Decay is
// the time the sound takes to fall 60 dB, DampHz how dark each time round makes it (a one-pole
// low-pass), Diffusion how much the start is smeared (a cave's walls give echoes you can pick out),
// and Wet brings each room's sound, for a long steady send with a music's spectrum (falling 6 dB an
// octave over 300 Hz, as a pulse's harmonics do), to 0.6 of the send's level.
struct RoomShape {
	double Size, Decay, DampHz, PreDelayMs, Diffusion, Wet;
};

const RoomShape ROOM_SHAPES[CSpace::ROOMS] = {
	{ 0.42, 0.55, 9000.0,  3.0, 0.65, 0.328 },	// a small room
	{ 1.00, 1.90, 7000.0, 16.0, 0.60, 0.274 },	// a hall
	{ 1.75, 4.00, 8000.0, 30.0, 0.70, 0.245 },	// a cathedral
	{ 1.35, 3.00, 3500.0, 22.0, 0.35, 0.263 },	// a cave
	{ 0.00, 0.00,    0.0,  0.0, 0.00, 0.000 },	// none: the echo alone
};

// The lines' lengths at 48 kHz and a size of 1 (primes, so their echoes don't line up)
const int LINE_LENGTHS[CSpace::LINES] = { 1123, 1291, 1447, 1613, 1777, 1949, 2131, 2297 };
const int DIFFUSER_LENGTHS[2] = { 142, 379 };
const double BIGGEST_ROOM = 1.75, LONGEST_PRE_DELAY = 0.04;
// How the reverb's input goes into its lines, and how each side hears them (two rows of a Hadamard
// matrix: the sides come out unlike each other)
const float INJECT[CSpace::LINES] = { 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f };
const float ECHO_LEVEL = 0.6f;				// the first repeat, of the send
const double ECHO_DAMP_HZ = 5000.0;			// each repeat a little darker
const float TRACE = 0.25f;					// under a quarter of the output's step: silent

inline void Hadamard8(float *x)
{
	for (int h = 1; h < 8; h <<= 1)
		for (int i = 0; i < 8; i += h << 1)
			for (int j = i; j < i + h; ++j) {
				const float a = x[j], b = x[j + h];
				x[j] = a + b;
				x[j + h] = a - b;
			}
	for (int i = 0; i < 8; ++i)
		x[i] *= 0.35355339f;	// 1 / sqrt(8): every time round as loud as before, but for the lines' gains
}

inline float OnePole(double Hz, unsigned SampleRate)
{
	return static_cast<float>(1.0 - std::exp(-2.0 * PI * Hz / std::max(1u, SampleRate)));
}

}	// namespace

// Soft

double CSoftFilter::Cutoff(int Soft)
{
	Soft = std::max(1, std::min(100, Soft));
	return SOFT_TOP * std::pow(SOFT_BOTTOM / SOFT_TOP, Soft / 100.0);
}

void CSoftFilter::Set(int Soft, unsigned SampleRate)
{
	Soft = std::max(0, std::min(100, Soft));
	if (Soft == m_iSoft && Soft == 0)
		return;
	m_iSoft = Soft;
	m_fA = Soft == 0 ? 1.0f : OnePole(Cutoff(Soft), SampleRate);
}

// Space

const double CSpace::LONGEST_ECHO = 2.0;

CSpace::CSpace() : m_iSampleRate(0), m_iRoom(1), m_iEchoSamples(0), m_iRepeats(3), m_dEchoSeconds(0.0), m_bIdle(true), m_iQuiet(0),
	m_fDamp(1.0f), m_fDiffusion(0.0f), m_fWet(0.0f), m_fEchoFeedback(0.0f), m_fEchoDamp(1.0f)
{
	for (int i = 0; i < LINES; ++i) {
		m_fLineGain[i] = 0.0f;
		m_fLineState[i] = 0.0f;
	}
	m_fEchoState[0] = m_fEchoState[1] = 0.0f;
}

CSpace::~CSpace()
{
	for (int i = 0; i < LINES; ++i)
		Free(m_Line[i]);
	Free(m_PreDelay);
	Free(m_Diffuse[0]);
	Free(m_Diffuse[1]);
	Free(m_Echo[0]);
	Free(m_Echo[1]);
}

bool CSpace::Make(Ring &R, int Size)
{
	Free(R);
	R.pData = new float[static_cast<size_t>(Size)];
	R.Size = Size;
	R.Length = 1;
	R.Pos = 0;
	std::memset(R.pData, 0, sizeof(float) * static_cast<size_t>(Size));
	return true;
}

void CSpace::Free(Ring &R)
{
	delete [] R.pData;
	R.pData = 0;
	R.Size = 0;
	R.Length = 1;
	R.Pos = 0;
}

bool CSpace::Allocate(unsigned SampleRate)
{
	if (SampleRate == 0)
		return false;
	m_iSampleRate = SampleRate;
	const double Scale = SampleRate / 48000.0;
	for (int i = 0; i < LINES; ++i)
		Make(m_Line[i], static_cast<int>(std::ceil(LINE_LENGTHS[i] * BIGGEST_ROOM * Scale)) + 2);
	Make(m_PreDelay, static_cast<int>(std::ceil(LONGEST_PRE_DELAY * SampleRate)) + 2);
	for (int k = 0; k < 2; ++k) {
		Make(m_Diffuse[k], static_cast<int>(std::ceil(DIFFUSER_LENGTHS[k] * Scale)) + 2);
		Make(m_Echo[k], static_cast<int>(std::ceil(LONGEST_ECHO * SampleRate)) + 2);
	}
	Setup();
	Clear();
	return true;
}

void CSpace::Clear()
{
	for (int i = 0; i < LINES; ++i) {
		if (m_Line[i].pData != 0)
			std::memset(m_Line[i].pData, 0, sizeof(float) * static_cast<size_t>(m_Line[i].Size));
		m_Line[i].Pos = 0;
		m_fLineState[i] = 0.0f;
	}
	Ring *const Others[5] = { &m_PreDelay, &m_Diffuse[0], &m_Diffuse[1], &m_Echo[0], &m_Echo[1] };
	for (int k = 0; k < 5; ++k) {
		if (Others[k]->pData != 0)
			std::memset(Others[k]->pData, 0, sizeof(float) * static_cast<size_t>(Others[k]->Size));
		Others[k]->Pos = 0;
	}
	m_fEchoState[0] = m_fEchoState[1] = 0.0f;
	m_iQuiet = 0;
	m_bIdle = true;
}

double CSpace::Feedback(int Repeats)
{
	Repeats = std::max(1, std::min(8, Repeats));
	return std::pow(10.0, -30.0 / (20.0 * Repeats));
}

void CSpace::Set(int Room, double EchoSeconds, int Repeats)
{
	Room = std::max(0, std::min(ROOMS - 1, Room));
	EchoSeconds = std::max(0.0, std::min(LONGEST_ECHO, EchoSeconds));
	Repeats = std::max(1, std::min(8, Repeats));
	if (Room == m_iRoom && EchoSeconds == m_dEchoSeconds && Repeats == m_iRepeats)
		return;
	const bool NewRoom = Room != m_iRoom, NewEcho = EchoSeconds != m_dEchoSeconds;
	m_iRoom = Room;
	m_dEchoSeconds = EchoSeconds;
	m_iRepeats = Repeats;
	if (NewRoom || NewEcho) {
		// The lines change their lengths: that part starts silent
		Ring *const Rings[LINES + 3] = { &m_Line[0], &m_Line[1], &m_Line[2], &m_Line[3], &m_Line[4], &m_Line[5], &m_Line[6], &m_Line[7],
			&m_PreDelay, &m_Diffuse[0], &m_Diffuse[1] };
		for (int k = 0; k < (NewRoom ? LINES + 3 : 0); ++k) {
			if (Rings[k]->pData != 0)
				std::memset(Rings[k]->pData, 0, sizeof(float) * static_cast<size_t>(Rings[k]->Size));
			Rings[k]->Pos = 0;
		}
		for (int k = 0; k < (NewEcho ? 2 : 0); ++k) {
			if (m_Echo[k].pData != 0)
				std::memset(m_Echo[k].pData, 0, sizeof(float) * static_cast<size_t>(m_Echo[k].Size));
			m_Echo[k].Pos = 0;
			m_fEchoState[k] = 0.0f;
		}
		if (NewRoom)
			for (int i = 0; i < LINES; ++i)
				m_fLineState[i] = 0.0f;
	}
	Setup();
}

void CSpace::Setup()
{
	if (m_iSampleRate == 0)
		return;
	const RoomShape &Shape = ROOM_SHAPES[m_iRoom];
	const double Scale = m_iSampleRate / 48000.0;
	for (int i = 0; i < LINES; ++i) {
		const int Length = std::max(1, static_cast<int>(std::floor(LINE_LENGTHS[i] * Shape.Size * Scale + 0.5)));
		m_Line[i].Length = std::min(std::max(1, m_Line[i].Size), Length);
		m_Line[i].Pos = std::min(m_Line[i].Pos, m_Line[i].Length - 1);
		m_fLineGain[i] = Shape.Decay > 0.0 ? static_cast<float>(std::pow(10.0, -3.0 * m_Line[i].Length / (Shape.Decay * m_iSampleRate))) : 0.0f;
	}
	m_PreDelay.Length = std::min(std::max(1, m_PreDelay.Size), std::max(1, static_cast<int>(std::floor(Shape.PreDelayMs * m_iSampleRate / 1000.0 + 0.5))));
	m_PreDelay.Pos = std::min(m_PreDelay.Pos, m_PreDelay.Length - 1);
	for (int k = 0; k < 2; ++k) {
		m_Diffuse[k].Length = std::min(std::max(1, m_Diffuse[k].Size), std::max(1, static_cast<int>(std::floor(DIFFUSER_LENGTHS[k] * Scale + 0.5))));
		m_Diffuse[k].Pos = std::min(m_Diffuse[k].Pos, m_Diffuse[k].Length - 1);
	}
	m_fDamp = Shape.DampHz > 0.0 ? OnePole(Shape.DampHz, m_iSampleRate) : 1.0f;
	m_fDiffusion = static_cast<float>(Shape.Diffusion);
	m_fWet = static_cast<float>(Shape.Wet);

	m_iEchoSamples = m_dEchoSeconds > 0.0 ? std::max(1, static_cast<int>(std::floor(m_dEchoSeconds * m_iSampleRate + 0.5))) : 0;
	for (int k = 0; k < 2; ++k) {
		m_Echo[k].Length = std::min(std::max(1, m_Echo[k].Size), std::max(1, m_iEchoSamples));
		m_Echo[k].Pos = std::min(m_Echo[k].Pos, m_Echo[k].Length - 1);
	}
	if (m_iEchoSamples > 0)
		m_iEchoSamples = m_Echo[0].Length;
	m_fEchoFeedback = static_cast<float>(Feedback(m_iRepeats));
	m_fEchoDamp = OnePole(ECHO_DAMP_HZ, m_iSampleRate);
}

void CSpace::Process(const float *pSendLeft, const float *pSendRight, float *pLeft, float *pRight, int Count)
{
	if (m_iSampleRate == 0 || m_Line[0].pData == 0 || Count <= 0)
		return;
	const bool Reverb = m_iRoom != ROOM_NONE, Echo = m_iEchoSamples > 0;
	if (m_bIdle) {
		// Silent until something is sent (and nothing to do without a room or an echo)
		bool Sent = false;
		for (int n = 0; n < Count && !Sent; ++n)
			Sent = pSendLeft[n] != 0.0f || pSendRight[n] != 0.0f;
		if (!Sent || (!Reverb && !Echo))
			return;
		m_bIdle = false;
		m_iQuiet = 0;
	}

	for (int n = 0; n < Count; ++n) {
		const float SendLeft = pSendLeft[n], SendRight = pSendRight[n];
		float OutLeft = 0.0f, OutRight = 0.0f, RepeatLeft = 0.0f, RepeatRight = 0.0f;
		if (Echo) {
			// What went in the echo's time ago, with the repeats before it, a little darker each time
			float *const pL = &m_Echo[0].pData[m_Echo[0].Pos], *const pR = &m_Echo[1].pData[m_Echo[1].Pos];
			m_fEchoState[0] += m_fEchoDamp * (*pL - m_fEchoState[0]);
			m_fEchoState[1] += m_fEchoDamp * (*pR - m_fEchoState[1]);
			RepeatLeft = m_fEchoState[0];
			RepeatRight = m_fEchoState[1];
			*pL = SendLeft + m_fEchoFeedback * RepeatLeft;
			*pR = SendRight + m_fEchoFeedback * RepeatRight;
			if (++m_Echo[0].Pos >= m_Echo[0].Length)
				m_Echo[0].Pos = 0;
			if (++m_Echo[1].Pos >= m_Echo[1].Length)
				m_Echo[1].Pos = 0;
			RepeatLeft *= ECHO_LEVEL;
			RepeatRight *= ECHO_LEVEL;
			OutLeft = RepeatLeft;
			OutRight = RepeatRight;
		}
		if (Reverb) {
			// The send and the echo's repeats, after the pre-delay, smeared by two all-passes
			float *const pPre = &m_PreDelay.pData[m_PreDelay.Pos];
			float In = *pPre;
			*pPre = 0.5f * (SendLeft + SendRight + RepeatLeft + RepeatRight);
			if (++m_PreDelay.Pos >= m_PreDelay.Length)
				m_PreDelay.Pos = 0;
			for (int k = 0; k < 2; ++k) {
				Ring &A = m_Diffuse[k];
				const float Delayed = A.pData[A.Pos];
				const float Out = Delayed - m_fDiffusion * In;
				A.pData[A.Pos] = In + m_fDiffusion * Out;
				if (++A.Pos >= A.Length)
					A.Pos = 0;
				In = Out;
			}
			// The lines, each darker and quieter, heard on the two sides, then mixed and fed back with the input
			float o[LINES];
			for (int i = 0; i < LINES; ++i) {
				m_fLineState[i] += m_fDamp * (m_Line[i].pData[m_Line[i].Pos] - m_fLineState[i]);
				o[i] = m_fLineState[i] * m_fLineGain[i];
			}
			const float Left = o[0] - o[1] + o[2] - o[3] + o[4] - o[5] + o[6] - o[7];
			const float Right = o[0] + o[1] - o[2] - o[3] + o[4] + o[5] - o[6] - o[7];
			Hadamard8(o);
			for (int i = 0; i < LINES; ++i) {
				m_Line[i].pData[m_Line[i].Pos] = o[i] + In * INJECT[i];
				if (++m_Line[i].Pos >= m_Line[i].Length)
					m_Line[i].Pos = 0;
			}
			OutLeft += m_fWet * Left;
			OutRight += m_fWet * Right;
		}
		pLeft[n] += OutLeft;
		pRight[n] += OutRight;
		if (SendLeft != 0.0f || SendRight != 0.0f || std::fabs(OutLeft) > TRACE || std::fabs(OutRight) > TRACE)
			m_iQuiet = 0;
		else
			++m_iQuiet;
	}

	// Silent for longer than anything takes to come round: nothing is left in the lines
	long Hold = m_PreDelay.Length + m_Diffuse[0].Length + m_Diffuse[1].Length + m_iEchoSamples + 64;
	for (int i = 0; i < LINES; ++i)
		Hold = std::max(Hold, static_cast<long>(m_Line[i].Length) * 4 + m_PreDelay.Length + m_iEchoSamples);
	if (m_iQuiet > Hold)
		Clear();
}

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

#ifndef SPACE_H
#define SPACE_H

// Depth for the mixer window (this FamiTracker's own; the NES has nothing of
// the kind): Soft, a gentle low-pass on a channel, and Space, a send from
// channels to one shared stereo space, a reverb with an echo, returned into
// both sides. CMixer runs them on the channels' own sound (a channel with
// either on is mixed into Blip_Buffers of its own), in playback, WAV files
// and the game's OGG files; NSF files don't have them. Plain C++, no MFC.
// Samples are floats in the units of the 16-bit output. Everything is set
// up by Allocate (not on the sound thread's loop); Process allocates nothing.

// Soft: a one-pole low-pass on both sides, from 12 kHz (1) down to 1.8 kHz
// (100). It rounds a pulse's or a sawtooth's edges and takes about 2.5 dB
// (50) to 7 dB (100) off the sound over 2 kHz, keeping it brighter than a
// triangle at the same pitch (the triangle's harmonics fall 12 dB an octave
// from the first; a pulse through it falls 6 dB an octave up to the cutoff).
class CSoftFilter {
public:
	CSoftFilter() : m_iSoft(0), m_fA(1.0f), m_fLeft(0.0f), m_fRight(0.0f) {}

	void Set(int Soft, unsigned SampleRate);	// 0 (off) to 100
	int Soft() const { return m_iSoft; }
	void Clear() { m_fLeft = m_fRight = 0.0f; }
	void Run(float &Left, float &Right)
	{
		// The tiny offset keeps the filter out of denormal numbers when the channel is silent
		m_fLeft += m_fA * (Left + 1e-15f - m_fLeft);
		m_fRight += m_fA * (Right + 1e-15f - m_fRight);
		Left = m_fLeft;
		Right = m_fRight;
	}
	static double Cutoff(int Soft);				// Hz

private:
	int m_iSoft;
	float m_fA;
	float m_fLeft, m_fRight;
};

// The space: a reverb of eight delay lines mixed through a Hadamard matrix
// (each line darker and quieter every time round, for the room's decay),
// after a pre-delay and two all-passes that smear the start, and an echo of
// its own (the send delayed, again and again, each repeat darker) whose
// repeats go into the reverb too. The rooms are numbered as
// ftapi::SPACE_ROOM_NAMES has them (append only): a small room, a hall, a
// cathedral, a cave, and none (the echo alone).
class CSpace {
public:
	static const int ROOMS = 5;
	static const int ROOM_NONE = 4;
	static const int LINES = 8;
	static const double LONGEST_ECHO;			// seconds

	CSpace();
	~CSpace();

	bool Allocate(unsigned SampleRate);			// the lines for the biggest room and the longest echo
	void Clear();								// silent, as a song starts
	// The room, the echo's time (0 for none) and its repeats (1-8); a change starts that part silent
	void Set(int Room, double EchoSeconds, int Repeats);
	// Count samples of the sends (each side) in, the space's sound added to Left and Right. Without
	// sends while it's silent it does nothing.
	void Process(const float *pSendLeft, const float *pSendRight, float *pLeft, float *pRight, int Count);
	bool Idle() const { return m_bIdle; }		// silent and nothing in its lines
	int Room() const { return m_iRoom; }		// as Set was given them
	double EchoSeconds() const { return m_dEchoSeconds; }
	int Repeats() const { return m_iRepeats; }

	// What a repeat of the echo is of the one before it, for a number of repeats: the repeat after
	// the last comes 30 dB under the first
	static double Feedback(int Repeats);

private:
	struct Ring {
		float *pData;
		int Size, Length, Pos;
		Ring() : pData(0), Size(0), Length(1), Pos(0) {}
	};
	bool Make(Ring &R, int Size);
	void Free(Ring &R);
	void Setup();								// the lengths and gains for the room and the echo

	unsigned m_iSampleRate;
	int m_iRoom, m_iEchoSamples, m_iRepeats;
	double m_dEchoSeconds;
	bool m_bIdle;
	long m_iQuiet;								// samples since a send or the sound was last over a trace

	Ring m_Line[LINES], m_PreDelay, m_Diffuse[2], m_Echo[2];
	float m_fLineGain[LINES], m_fLineState[LINES];
	float m_fDamp, m_fDiffusion, m_fWet;
	float m_fEchoFeedback, m_fEchoDamp, m_fEchoState[2];

	CSpace(const CSpace &);
	CSpace &operator=(const CSpace &);
};

#endif /* SPACE_H */

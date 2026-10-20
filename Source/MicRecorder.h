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

// Microphone input and click output for the Record dialog, through the
// Windows multimedia (waveIn/waveOut) API. Both are polled from the UI thread:
// enough buffers are queued that a poll every few tens of milliseconds keeps
// up. The Windows parts live in MicRecorder.cpp, so this header needs no
// Windows headers and can come before stdafx.h, like the standard library.

#include <string>
#include <vector>

namespace transcribe { struct ClickTrack; }

// 16-bit mono recording from a microphone
class CMicInput
{
public:
	CMicInput();
	~CMicInput();

	static int DeviceCount();
	static std::string DeviceName(int Device);	// as Windows calls it

	// Starts recording from Device (0 .. DeviceCount() - 1, or -1 for the
	// microphone picked in Windows). Returns false with the reason in Error.
	bool Open(int Device, int SampleRate, std::string &Error);
	void Close();
	bool IsOpen() const;
	int SampleRate() const;

	// Collects what was recorded since the last call: appended to Samples when
	// given, otherwise dropped. Returns the loudest sample in it, 0-1.
	float Poll(std::vector<short> *pSamples);

	long long Received() const;		// samples collected by Poll since Open
	long long Position() const;		// samples recorded so far as Windows counts them, -1 if unknown

private:
	struct Impl;
	Impl *m_pImpl;

	CMicInput(const CMicInput &);
	CMicInput &operator=(const CMicInput &);
};

// Plays a click track on the default output, from its first sample
class CClickOutput
{
public:
	CClickOutput();
	~CClickOutput();

	// Starts playing at once. Returns false with the reason in Error.
	bool Open(const transcribe::ClickTrack &Click, std::string &Error);
	void Close();
	bool IsOpen() const;

	void Poll();					// queues more of the click track
	long long Position() const;		// samples played so far as Windows counts them, -1 if unknown
	long long Queued() const;		// samples handed to Windows so far

private:
	struct Impl;
	Impl *m_pImpl;

	CClickOutput(const CClickOutput &);
	CClickOutput &operator=(const CClickOutput &);
};

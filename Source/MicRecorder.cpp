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

// Microphone input and click output for the Record dialog (see MicRecorder.h).
//
// Both use the waveIn/waveOut API without callbacks: Windows marks a buffer
// WHDR_DONE when it has been recorded or played, and Poll() hands it back.

// Standard and engine headers come before stdafx.h, whose DEBUG_NEW macro
// would otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "Transcribe/Transcribe.h"
#include "stdafx.h"
#include <mmsystem.h>
#include "MicRecorder.h"

namespace {

// Short input buffers, so Windows' count of recorded samples moves in small
// steps; two seconds of recording can wait for a poll. A second of click is
// queued ahead.
const int INPUT_BUFFER_MS = 20;
const int INPUT_BUFFERS = 100;
const int OUTPUT_BUFFER_MS = 50;
const int OUTPUT_BUFFERS = 20;

// Windows sets WHDR_DONE from its own thread
bool IsDone(const WAVEHDR &Header)
{
	return (*static_cast<const volatile DWORD*>(&Header.dwFlags) & WHDR_DONE) != 0;
}

WAVEFORMATEX MonoFormat(int SampleRate)
{
	WAVEFORMATEX Format;
	std::memset(&Format, 0, sizeof(Format));
	Format.wFormatTag = WAVE_FORMAT_PCM;
	Format.nChannels = 1;
	Format.nSamplesPerSec = static_cast<DWORD>(SampleRate);
	Format.wBitsPerSample = 16;
	Format.nBlockAlign = 2;
	Format.nAvgBytesPerSec = static_cast<DWORD>(SampleRate) * 2;
	Format.cbSize = 0;
	return Format;
}

long long SamplesOf(const MMTIME &Time)
{
	if (Time.wType == TIME_SAMPLES)
		return static_cast<long long>(Time.u.sample);
	if (Time.wType == TIME_BYTES)
		return static_cast<long long>(Time.u.cb) / 2;
	return -1;
}

}	// namespace

// ---------------------------------------------------------------------------
// Microphone
// ---------------------------------------------------------------------------

struct CMicInput::Impl
{
	HWAVEIN hWaveIn;
	int Rate;
	std::vector<WAVEHDR> Headers;
	std::vector<std::vector<short> > Buffers;
	size_t Next;			// the buffer Windows fills next
	long long Received;

	Impl() : hWaveIn(NULL), Rate(0), Next(0), Received(0) {}
};

CMicInput::CMicInput() : m_pImpl(new Impl())
{
}

CMicInput::~CMicInput()
{
	Close();
	delete m_pImpl;
}

int CMicInput::DeviceCount()
{
	return static_cast<int>(waveInGetNumDevs());
}

std::string CMicInput::DeviceName(int Device)
{
	WAVEINCAPSA Caps;
	std::memset(&Caps, 0, sizeof(Caps));
	if (Device < 0 || waveInGetDevCapsA(static_cast<UINT_PTR>(Device), &Caps, sizeof(Caps)) != MMSYSERR_NOERROR)
		return std::string();
	return std::string(Caps.szPname);
}

bool CMicInput::Open(int Device, int SampleRate, std::string &Error)
{
	Close();
	if (waveInGetNumDevs() == 0) {
		Error = "No microphone was found. Plug one in, or switch it on in the Windows sound settings.";
		return false;
	}

	WAVEFORMATEX Format = MonoFormat(SampleRate);
	const UINT Id = Device < 0 ? WAVE_MAPPER : static_cast<UINT>(Device);
	const MMRESULT Result = waveInOpen(&m_pImpl->hWaveIn, Id, &Format, 0, 0, CALLBACK_NULL);
	if (Result != MMSYSERR_NOERROR) {
		m_pImpl->hWaveIn = NULL;
		char Text[MAXERRORLENGTH] = "";
		waveInGetErrorTextA(Result, Text, MAXERRORLENGTH);
		Error = "The microphone couldn't be opened: ";
		Error += Text;
		return false;
	}

	m_pImpl->Rate = SampleRate;
	m_pImpl->Next = 0;
	m_pImpl->Received = 0;
	const size_t Samples = static_cast<size_t>(SampleRate) * INPUT_BUFFER_MS / 1000;
	m_pImpl->Buffers.assign(INPUT_BUFFERS, std::vector<short>(Samples));
	m_pImpl->Headers.resize(INPUT_BUFFERS);
	for (int i = 0; i < INPUT_BUFFERS; ++i) {
		WAVEHDR &Header = m_pImpl->Headers[i];
		std::memset(&Header, 0, sizeof(Header));
		Header.lpData = reinterpret_cast<LPSTR>(&m_pImpl->Buffers[i][0]);
		Header.dwBufferLength = static_cast<DWORD>(Samples * sizeof(short));
		waveInPrepareHeader(m_pImpl->hWaveIn, &Header, sizeof(WAVEHDR));
		waveInAddBuffer(m_pImpl->hWaveIn, &Header, sizeof(WAVEHDR));
	}
	if (waveInStart(m_pImpl->hWaveIn) != MMSYSERR_NOERROR) {
		Close();
		Error = "The microphone couldn't be started.";
		return false;
	}
	return true;
}

void CMicInput::Close()
{
	if (m_pImpl->hWaveIn == NULL)
		return;
	waveInReset(m_pImpl->hWaveIn);	// hands every buffer back
	for (size_t i = 0; i < m_pImpl->Headers.size(); ++i)
		waveInUnprepareHeader(m_pImpl->hWaveIn, &m_pImpl->Headers[i], sizeof(WAVEHDR));
	waveInClose(m_pImpl->hWaveIn);
	m_pImpl->hWaveIn = NULL;
	m_pImpl->Headers.clear();
	m_pImpl->Buffers.clear();
	m_pImpl->Next = 0;
}

bool CMicInput::IsOpen() const
{
	return m_pImpl->hWaveIn != NULL;
}

int CMicInput::SampleRate() const
{
	return m_pImpl->Rate;
}

float CMicInput::Poll(std::vector<short> *pSamples)
{
	int Peak = 0;
	if (m_pImpl->hWaveIn == NULL)
		return 0.0f;

	// Buffers come back in the order they were added
	for (int n = 0; n < INPUT_BUFFERS; ++n) {
		WAVEHDR &Header = m_pImpl->Headers[m_pImpl->Next];
		if (!IsDone(Header))
			break;
		const DWORD Count = static_cast<DWORD>(Header.dwBytesRecorded / sizeof(short));
		const short *pData = reinterpret_cast<const short*>(Header.lpData);
		for (DWORD i = 0; i < Count; ++i)
			Peak = std::max(Peak, std::abs(static_cast<int>(pData[i])));
		if (pSamples != NULL)
			pSamples->insert(pSamples->end(), pData, pData + Count);
		m_pImpl->Received += Count;

		Header.dwFlags &= ~WHDR_DONE;
		Header.dwBytesRecorded = 0;
		waveInAddBuffer(m_pImpl->hWaveIn, &Header, sizeof(WAVEHDR));
		m_pImpl->Next = (m_pImpl->Next + 1) % m_pImpl->Headers.size();
	}
	return static_cast<float>(Peak) / 32768.0f;
}

long long CMicInput::Received() const
{
	return m_pImpl->Received;
}

long long CMicInput::Position() const
{
	if (m_pImpl->hWaveIn == NULL)
		return -1;
	MMTIME Time;
	std::memset(&Time, 0, sizeof(Time));
	Time.wType = TIME_SAMPLES;
	if (waveInGetPosition(m_pImpl->hWaveIn, &Time, sizeof(Time)) != MMSYSERR_NOERROR)
		return -1;
	return SamplesOf(Time);
}

// ---------------------------------------------------------------------------
// Click
// ---------------------------------------------------------------------------

struct CClickOutput::Impl
{
	HWAVEOUT hWaveOut;
	transcribe::ClickTrack Click;
	std::vector<WAVEHDR> Headers;
	std::vector<std::vector<short> > Buffers;
	std::vector<float> Rendered;
	size_t Next;			// the buffer Windows finishes next
	long long Queued;

	Impl() : hWaveOut(NULL), Next(0), Queued(0) {}

	// Renders the next stretch of the click into buffer i and queues it
	void Fill(size_t i)
	{
		WAVEHDR &Header = Headers[i];
		std::vector<short> &Buffer = Buffers[i];
		const int Count = static_cast<int>(Buffer.size());
		Click.Render(Queued, &Rendered[0], Count);
		for (int j = 0; j < Count; ++j) {
			const float v = std::max(-1.0f, std::min(1.0f, Rendered[j]));
			Buffer[j] = static_cast<short>(v * 32767.0f);
		}
		Header.dwFlags &= ~WHDR_DONE;
		waveOutWrite(hWaveOut, &Header, sizeof(WAVEHDR));
		Queued += Count;
	}
};

CClickOutput::CClickOutput() : m_pImpl(new Impl())
{
}

CClickOutput::~CClickOutput()
{
	Close();
	delete m_pImpl;
}

bool CClickOutput::Open(const transcribe::ClickTrack &Click, std::string &Error)
{
	Close();
	WAVEFORMATEX Format = MonoFormat(Click.sampleRate);
	const MMRESULT Result = waveOutOpen(&m_pImpl->hWaveOut, WAVE_MAPPER, &Format, 0, 0, CALLBACK_NULL);
	if (Result != MMSYSERR_NOERROR) {
		m_pImpl->hWaveOut = NULL;
		char Text[MAXERRORLENGTH] = "";
		waveOutGetErrorTextA(Result, Text, MAXERRORLENGTH);
		Error = "The click couldn't be played: ";
		Error += Text;
		return false;
	}

	m_pImpl->Click = Click;
	m_pImpl->Next = 0;
	m_pImpl->Queued = 0;
	const size_t Samples = static_cast<size_t>(Click.sampleRate) * OUTPUT_BUFFER_MS / 1000;
	m_pImpl->Buffers.assign(OUTPUT_BUFFERS, std::vector<short>(Samples));
	m_pImpl->Rendered.assign(Samples, 0.0f);
	m_pImpl->Headers.resize(OUTPUT_BUFFERS);

	// Queue a second of it before playing starts, so it starts without a gap
	waveOutPause(m_pImpl->hWaveOut);
	for (int i = 0; i < OUTPUT_BUFFERS; ++i) {
		WAVEHDR &Header = m_pImpl->Headers[i];
		std::memset(&Header, 0, sizeof(Header));
		Header.lpData = reinterpret_cast<LPSTR>(&m_pImpl->Buffers[i][0]);
		Header.dwBufferLength = static_cast<DWORD>(Samples * sizeof(short));
		waveOutPrepareHeader(m_pImpl->hWaveOut, &Header, sizeof(WAVEHDR));
		m_pImpl->Fill(static_cast<size_t>(i));
	}
	waveOutRestart(m_pImpl->hWaveOut);
	return true;
}

void CClickOutput::Close()
{
	if (m_pImpl->hWaveOut == NULL)
		return;
	waveOutReset(m_pImpl->hWaveOut);	// stops and hands every buffer back
	for (size_t i = 0; i < m_pImpl->Headers.size(); ++i)
		waveOutUnprepareHeader(m_pImpl->hWaveOut, &m_pImpl->Headers[i], sizeof(WAVEHDR));
	waveOutClose(m_pImpl->hWaveOut);
	m_pImpl->hWaveOut = NULL;
	m_pImpl->Headers.clear();
	m_pImpl->Buffers.clear();
	m_pImpl->Next = 0;
}

bool CClickOutput::IsOpen() const
{
	return m_pImpl->hWaveOut != NULL;
}

void CClickOutput::Poll()
{
	if (m_pImpl->hWaveOut == NULL)
		return;
	for (int n = 0; n < OUTPUT_BUFFERS; ++n) {
		if (!IsDone(m_pImpl->Headers[m_pImpl->Next]))
			break;
		m_pImpl->Fill(m_pImpl->Next);
		m_pImpl->Next = (m_pImpl->Next + 1) % m_pImpl->Headers.size();
	}
}

long long CClickOutput::Position() const
{
	if (m_pImpl->hWaveOut == NULL)
		return -1;
	MMTIME Time;
	std::memset(&Time, 0, sizeof(Time));
	Time.wType = TIME_SAMPLES;
	if (waveOutGetPosition(m_pImpl->hWaveOut, &Time, sizeof(Time)) != MMSYSERR_NOERROR)
		return -1;
	return SamplesOf(Time);
}

long long CClickOutput::Queued() const
{
	return m_pImpl->Queued;
}

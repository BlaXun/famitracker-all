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

// Ideas recorded on a phone (Edit > Record from your phone, RECORDER.md): a
// page FamiTracker serves (res/phone.html) records a hummed or whistled idea,
// or takes what the phone's own recorder made, and sends it as a 16-bit mono
// WAV file; FamiTracker keeps it in the Recordings folder and makes it a clip
// of the open song, transcribed as Edit > Record from microphone transcribes
// a take (PhoneLink.cpp). The routes, which HandleMcp (Mcp.h) passes on
// after its own checks (other computers only when the user allows them, from
// home networks, at this computer's addresses or name):
//
// - GET /phone?code=CODE: the page (the QR code carries this address).
// - GET /phone/takes: the takes sent so far, newest first, as JSON.
// - POST /phone/take?name=NAME: a take, the request's body a WAV file; the
//   reply says what becomes of it.
//
// Every request needs the phone page's own code (PhoneCode, worked out from
// the access code, so New access code changes it too): in the address or as
// "Authorization: Bearer CODE". The code drives nothing but these routes, so
// a phone's history (synced to the cloud, perhaps) never holds the code that
// drives the tools. A request from another web page (its Origin) is refused.

#include <cstddef>
#include <string>
#include <vector>
#include "Http.h"
#include "Json.h"

namespace ftapi {

const double MIN_PHONE_TAKE_SECONDS = 0.5;
const double MAX_PHONE_TAKE_SECONDS = 180.0;	// the page stops recording there
const size_t MAX_PHONE_TAKE_BYTES = 8000000;	// 3 minutes at the page's 22050 Hz and a header fit (HTTP takes 8 MiB)
const size_t MAX_IDEA_NAME = 60;				// bytes of UTF-8
const size_t MAX_PHONE_TAKES = 40;				// the takes the page lists; older ones stay in the folder

// SHA-256 (FIPS 180-4) and HMAC-SHA-256 (RFC 2104): 32 bytes each
std::string Sha256(const std::string &Bytes);
std::string HmacSha256(const std::string &Key, const std::string &Message);

// The phone page's code for an access code: 24 hex digits
std::string PhoneCode(const std::string &Token);
// The address the QR code carries: http://192.168.1.10:46600/phone?code=...
std::string PhonePageUrl(const std::string &Address, int Port, const std::string &Token);

struct PhoneWav {
	int SampleRate;
	size_t Samples;
	double Seconds;

	PhoneWav() : SampleRate(0), Samples(0), Seconds(0.0) {}
};

// A WAV file as the page sends it, checked before anything reads it: 16-bit
// mono PCM at 8000-48000 Hz, 0.5 s to 3 minutes, its chunks inside the file.
// False with the reason, in words for the phone.
bool CheckPhoneWav(const std::string &Bytes, PhoneWav &Info, std::string &Why);

// An idea's name as the phone gives it (spaces around it left out): valid
// UTF-8 of at most MAX_IDEA_NAME bytes, no control characters and none of
// \ / : * ? " < > |, since it names a file in the Recordings folder and a
// clip. Empty is fine: FamiTracker numbers the idea.
bool IsIdeaName(const std::string &Name);

enum PhoneTakeState { PHONE_WAITING, PHONE_CLIP, PHONE_FAILED };

struct PhoneTake {
	int Id;
	std::string Idea;		// the name given on the phone (UTF-8), or empty
	std::string File;		// its name in the Recordings folder
	std::string Path;
	double Seconds;
	std::string Arrived;	// "14:03"
	PhoneTakeState State;
	std::string Clip;		// the clip it became
	std::string Message;	// what becomes of it, for the page

	PhoneTake() : Id(0), Seconds(0.0), State(PHONE_WAITING) {}
};

// What FamiTracker does with the takes: PhoneLink.cpp, and a fake in the
// tests. Called on the thread that answers requests, which owns the song.
class PhoneHost {
public:
	virtual ~PhoneHost() {}
	// The take into the Recordings folder under the first of Names this
	// computer can make (each a plain file name, the last the plainest),
	// never over another file: its path and file name
	virtual bool SaveTake(const std::vector<std::string> &Names, const std::string &Bytes, std::string &Path, std::string &File,
		std::string &Error) = 0;
	// Whether the open song can take a clip now; false with the reason (a dialog is open...)
	virtual bool CanMakeClip(std::string &Why) = 0;
	// The take at Path transcribed and kept as a clip named after Idea ("Idea 1" when it's empty, a number added when
	// a clip has the name): the clip's name and what it holds in Message; false with the reason
	virtual bool MakeClip(const std::string &Path, const std::string &Idea, std::string &Clip, std::string &Message) = 0;
	virtual std::string Now() = 0;	// the local time, "2026-10-25 14-03-12"
	virtual void Wake() = 0;		// takes wait: Process() them soon, on this thread
};

class PhoneTakes {
public:
	explicit PhoneTakes(PhoneHost &Host);

	void SetPage(const std::string &Html);	// what GET /phone gives
	// A request to /phone or a route under it, its peer and Host checked by
	// HandleMcp; Token is the access code
	HttpResponse Handle(const HttpRequest &Request, const std::string &Token);
	// Makes the waiting takes clips, the oldest first, while the song can take
	// them; true while some still wait
	bool Process();
	bool Waiting() const;
	const std::vector<PhoneTake> &Takes() const { return m_Takes; }

private:
	HttpResponse Receive(const HttpRequest &Request, const std::string &Name);
	Json ListJson() const;

	PhoneHost &m_Host;
	std::string m_sPage;
	std::vector<PhoneTake> m_Takes;	// in the order they arrived
	int m_iNextId;
	bool m_bProcessing;

	PhoneTakes(const PhoneTakes &);
	PhoneTakes &operator=(const PhoneTakes &);
};

}	// namespace ftapi

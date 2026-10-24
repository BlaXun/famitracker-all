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

// The connection from other computers on the user's home network (File >
// Connect Claude Code, "Allow connections from other computers on my
// network"): who may connect, which Host headers name this computer, and the
// names and links of the files that travel over it (the Inbox, the exports).
// IPv4 only, as FamiTracker listens.

#include <cstddef>
#include <string>
#include <vector>

namespace ftapi {

// An IPv4 address as text ("192.168.1.10", four numbers 0-255 without leading
// zeros) to a number in host order (0xC0A8010A), and back
bool ParseIpv4(const std::string &Text, unsigned long &Address);
std::string Ipv4Text(unsigned long Address);

bool IsLoopbackIpv4(unsigned long Address);		// 127.0.0.0/8: this computer
// A home network's address: 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16,
// 169.254.0.0/16 (link-local), 100.64.0.0/10 (shared address space, which
// Tailscale gives its computers), or loopback
bool IsPrivateIpv4(unsigned long Address);

// Whether a client at Peer (its address as text) may connect when other
// computers are allowed: one with a home network's address, or this computer
// itself at one of its own addresses (Own), whatever kind they are
bool MayConnect(const std::string &Peer, const std::vector<std::string> &Own);

// A Host header that names this computer: one of its addresses
// ("192.168.1.10:46600"), or its name, alone or with a domain ("desktop-pc",
// "desktop-pc.fritz.box", "desktop-pc.tail1234.ts.net"), any case
bool NamesThisComputer(const std::string &Host, const std::vector<std::string> &Own, const std::string &ComputerName);

// A file's name without folders that Windows can make: valid UTF-8, no
// control characters or \ / : * ? " < > |, not "." or "..", no space or dot at
// its end, not a device's name (CON, NUL, COM1...), at most 200 bytes
bool IsPlainFileName(const std::string &Name);

// The files that may be sent to FamiTracker's Inbox (send_file, PUT /inbox):
// what study_nsf, import_nsf, study_module and make_sound_effect read
bool IsInboxFile(const std::string &Name);
const char *const INBOX_FILE_KINDS = ".nsf, .nsfe, .m3u, .mod, .s3m, .xm, .it, .mid, .midi, .rmi, .ftm or .wav";
const size_t MAX_INBOX_FILE = 5u << 20;		// 5 MB

// The files a client may fetch from the song's folder (GET /files): what
// export_for_game saves
bool IsExportFile(const std::string &Name);

// A name in a link, every byte but A-Z a-z 0-9 - . _ ~ as %XX; and a link's
// path back (false for a broken %)
std::string UrlEncode(const std::string &Text);
bool UrlDecode(const std::string &Text, std::string &Out);

// Base64 (RFC 4648) back to bytes, also its URL alphabet, with or without
// padding, line breaks and spaces skipped; false when it isn't base64
bool Base64Decode(const std::string &Text, std::string &Bytes);

}	// namespace ftapi

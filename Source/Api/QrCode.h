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

// QR codes (ISO/IEC 18004), drawn by FamiTracker itself for the address a
// phone scans (Edit > Record from your phone, PhoneDlg.cpp): bytes in byte
// mode, any version from 1 to 40, error correction L, M, Q or H, and the
// mask the standard's penalty rules pick. The tests compare its codes with
// codes a reference encoder made (tests/PhoneTests.cpp).

#include <string>
#include <vector>

namespace ftapi {

// How much of a code can be damaged and still read: about 7, 15, 25 and 30 %
enum QrLevel { QR_LOW, QR_MEDIUM, QR_QUARTILE, QR_HIGH };

struct QrSymbol {
	int Version;	// 1-40
	int Size;		// modules a side: 17 + 4 * Version
	QrLevel Level;
	int Mask;		// 0-7
	std::vector<unsigned char> Modules;	// Size * Size, a row at a time from the top; 1 is dark

	QrSymbol() : Version(0), Size(0), Level(QR_MEDIUM), Mask(0) {}
	bool Dark(int x, int y) const;		// x across, y down; false outside the symbol (the quiet zone around it)
};

// Bytes (an address, in UTF-8) as a QR code: the smallest version from
// MinVersion up that holds them at Level, then with Boost the highest level
// that still fits that version. Mask 0-7, or -1 for the one the penalty rules
// pick. False when they don't fit in version 40.
bool MakeQrSymbol(const std::string &Bytes, QrLevel Level, QrSymbol &Out, int Mask = -1, bool Boost = true, int MinVersion = 1);

}	// namespace ftapi

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

// PNG images for the Claude Code connection (the picture of the notes):
// palette images, compressed with a small deflate (LZ77 and the fixed
// Huffman codes), which suits pictures with large plain areas. Plain C++14.

#include <string>
#include <vector>

namespace ftapi {

struct Rgb {
	unsigned char R, G, B;
};

// Width x Height pixels from the top row down, each an index into Palette
// (at most 256 colors)
std::string EncodePng(int Width, int Height, const std::vector<unsigned char> &Pixels, const std::vector<Rgb> &Palette);

// The pieces, for the tests
std::string ZlibCompress(const std::string &Data);
unsigned long Crc32(const std::string &Data);
unsigned long Adler32(const std::string &Data);

}	// namespace ftapi

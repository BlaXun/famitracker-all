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

#include "Png.h"

#include <algorithm>

namespace ftapi {

namespace {

// Writes bits from the lowest up, as deflate packs them
class BitWriter {
public:
	BitWriter() : m_Bits(0), m_Count(0) {}

	void Put(unsigned long Value, int Count)
	{
		m_Bits |= (Value & ((1ul << Count) - 1)) << m_Count;
		m_Count += Count;
		while (m_Count >= 8) {
			m_Out += static_cast<char>(m_Bits & 0xFF);
			m_Bits >>= 8;
			m_Count -= 8;
		}
	}

	// A Huffman code, which deflate stores from its highest bit down
	void PutCode(unsigned long Code, int Length)
	{
		unsigned long Reversed = 0;
		for (int i = 0; i < Length; ++i)
			Reversed |= ((Code >> i) & 1ul) << (Length - 1 - i);
		Put(Reversed, Length);
	}

	std::string Finish()
	{
		if (m_Count > 0)
			Put(0, 8 - m_Count);
		return m_Out;
	}

private:
	std::string m_Out;
	unsigned long m_Bits;
	int m_Count;
};

// The fixed literal/length codes (RFC 1951, 3.2.6)
void PutSymbol(BitWriter &Out, int Symbol)
{
	if (Symbol < 144)
		Out.PutCode(0x30 + Symbol, 8);
	else if (Symbol < 256)
		Out.PutCode(0x190 + Symbol - 144, 9);
	else if (Symbol < 280)
		Out.PutCode(Symbol - 256, 7);
	else
		Out.PutCode(0xC0 + Symbol - 280, 8);
}

const int LENGTH_BASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
	131, 163, 195, 227, 258 };
const int LENGTH_EXTRA[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const int DISTANCE_BASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
	2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const int DISTANCE_EXTRA[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12,
	13, 13 };

void PutMatch(BitWriter &Out, int Length, int Distance)
{
	int Code = 28;
	while (LENGTH_BASE[Code] > Length)
		--Code;
	PutSymbol(Out, 257 + Code);
	Out.Put(static_cast<unsigned long>(Length - LENGTH_BASE[Code]), LENGTH_EXTRA[Code]);
	Code = 29;
	while (DISTANCE_BASE[Code] > Distance)
		--Code;
	Out.PutCode(static_cast<unsigned long>(Code), 5);
	Out.Put(static_cast<unsigned long>(Distance - DISTANCE_BASE[Code]), DISTANCE_EXTRA[Code]);
}

const int WINDOW = 32768, MIN_MATCH = 3, MAX_MATCH = 258, MAX_CHAIN = 96;
const int HASH_SIZE = 1 << 15;

// One deflate block with the fixed codes, matches found through hash chains
std::string Deflate(const std::string &Data)
{
	const unsigned char *p = reinterpret_cast<const unsigned char*>(Data.data());
	const int Size = static_cast<int>(Data.size());
	std::vector<int> Head(HASH_SIZE, -1), Previous(WINDOW, -1);
	auto Hash = [p](int i) {
		return static_cast<int>(((static_cast<unsigned>(p[i]) << 10) ^ (static_cast<unsigned>(p[i + 1]) << 5) ^ p[i + 2]) & (HASH_SIZE - 1));
	};
	auto Insert = [&](int i) {
		if (i + MIN_MATCH <= Size) {
			const int h = Hash(i);
			Previous[i % WINDOW] = Head[h];
			Head[h] = i;
		}
	};

	BitWriter Out;
	Out.Put(1, 1);	// the last block
	Out.Put(1, 2);	// fixed codes
	for (int i = 0; i < Size; ) {
		int Best = 0, BestDistance = 0;
		if (i + MIN_MATCH <= Size) {
			const int Longest = std::min(MAX_MATCH, Size - i);
			int Candidate = Head[Hash(i)];
			for (int Chain = 0; Candidate >= 0 && i - Candidate <= WINDOW && Chain < MAX_CHAIN; ++Chain) {
				if (p[Candidate + Best] == p[i + Best]) {
					int Length = 0;
					while (Length < Longest && p[Candidate + Length] == p[i + Length])
						++Length;
					if (Length > Best) {
						Best = Length;
						BestDistance = i - Candidate;
						if (Length == Longest)
							break;
					}
				}
				Candidate = Previous[Candidate % WINDOW];
			}
		}
		if (Best >= MIN_MATCH) {
			PutMatch(Out, Best, BestDistance);
			for (int k = 0; k < Best; ++k)
				Insert(i + k);
			i += Best;
		}
		else {
			PutSymbol(Out, p[i]);
			Insert(i);
			++i;
		}
	}
	PutSymbol(Out, 256);	// end of the block
	return Out.Finish();
}

void PutBigEndian(std::string &Out, unsigned long Value)
{
	Out += static_cast<char>((Value >> 24) & 0xFF);
	Out += static_cast<char>((Value >> 16) & 0xFF);
	Out += static_cast<char>((Value >> 8) & 0xFF);
	Out += static_cast<char>(Value & 0xFF);
}

void PutChunk(std::string &Out, const char *pType, const std::string &Data)
{
	PutBigEndian(Out, static_cast<unsigned long>(Data.size()));
	const std::string Typed = std::string(pType, 4) + Data;
	Out += Typed;
	PutBigEndian(Out, Crc32(Typed));
}

}	// namespace

unsigned long Crc32(const std::string &Data)
{
	static const std::vector<unsigned long> Table = [] {
		std::vector<unsigned long> Values(256);
		for (unsigned long n = 0; n < 256; ++n) {
			unsigned long c = n;
			for (int k = 0; k < 8; ++k)
				c = (c & 1) ? 0xEDB88320ul ^ (c >> 1) : c >> 1;
			Values[n] = c;
		}
		return Values;
	}();
	unsigned long Crc = 0xFFFFFFFFul;
	for (size_t i = 0; i < Data.size(); ++i)
		Crc = Table[(Crc ^ static_cast<unsigned char>(Data[i])) & 0xFF] ^ (Crc >> 8);
	return (Crc ^ 0xFFFFFFFFul) & 0xFFFFFFFFul;
}

unsigned long Adler32(const std::string &Data)
{
	unsigned long a = 1, b = 0;
	for (size_t i = 0; i < Data.size(); ++i) {
		a = (a + static_cast<unsigned char>(Data[i])) % 65521;
		b = (b + a) % 65521;
	}
	return (b << 16) | a;
}

std::string ZlibCompress(const std::string &Data)
{
	std::string Out;
	Out += static_cast<char>(0x78);	// deflate, 32K window
	Out += static_cast<char>(0x01);	// check bits for 0x78
	Out += Deflate(Data);
	PutBigEndian(Out, Adler32(Data));
	return Out;
}

std::string EncodePng(int Width, int Height, const std::vector<unsigned char> &Pixels, const std::vector<Rgb> &Palette)
{
	if (Width < 1 || Height < 1 || Pixels.size() < static_cast<size_t>(Width) * static_cast<size_t>(Height) || Palette.empty())
		return std::string();
	std::string Out("\x89PNG\r\n\x1A\n", 8);
	std::string Header;
	PutBigEndian(Header, static_cast<unsigned long>(Width));
	PutBigEndian(Header, static_cast<unsigned long>(Height));
	Header += static_cast<char>(8);		// bits per pixel
	Header += static_cast<char>(3);		// palette colors
	Header += std::string(3, '\0');		// deflate, adaptive filters, no interlace
	PutChunk(Out, "IHDR", Header);

	std::string Colors;
	for (size_t i = 0; i < Palette.size() && i < 256; ++i) {
		Colors += static_cast<char>(Palette[i].R);
		Colors += static_cast<char>(Palette[i].G);
		Colors += static_cast<char>(Palette[i].B);
	}
	PutChunk(Out, "PLTE", Colors);

	// Each row as it is (filter 0); repeated rows compress to back references
	std::string Raw;
	Raw.reserve(static_cast<size_t>(Height) * (static_cast<size_t>(Width) + 1));
	for (int y = 0; y < Height; ++y) {
		Raw += '\0';
		Raw.append(reinterpret_cast<const char*>(&Pixels[static_cast<size_t>(y) * static_cast<size_t>(Width)]), static_cast<size_t>(Width));
	}
	PutChunk(Out, "IDAT", ZlibCompress(Raw));
	PutChunk(Out, "IEND", std::string());
	return Out;
}

}	// namespace ftapi

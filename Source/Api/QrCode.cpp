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

#include "QrCode.h"

#include <algorithm>
#include <cstdlib>

namespace ftapi {

namespace {

// Error correction codewords in each block, and the number of blocks, by
// level (L, M, Q, H) and version (ISO/IEC 18004, table 9; index 0 unused)
const signed char ECC_PER_BLOCK[4][41] = {
	{ -1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28,
		28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },
	{ -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26,
		26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28 },
	{ -1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30,
		28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },
	{ -1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28,
		30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },
};
const signed char ECC_BLOCKS[4][41] = {
	{ -1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8,
		8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25 },
	{ -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16,
		17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49 },
	{ -1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20,
		23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68 },
	{ -1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25,
		25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81 },
};

// The two bits the format information gives each level
const int LEVEL_BITS[4] = { 1, 0, 3, 2 };

// The penalty rules' weights: runs of five or more, 2x2 blocks, patterns
// like a finder's, and the share of dark modules
const int PENALTY_RUN = 3, PENALTY_BLOCK = 3, PENALTY_FINDER = 40, PENALTY_BALANCE = 10;

// A version's modules for data and error correction (all but the function
// patterns and the format and version information)
int RawModules(int Version)
{
	int Result = (16 * Version + 128) * Version + 64;
	if (Version >= 2) {
		const int Alignments = Version / 7 + 2;
		Result -= (25 * Alignments - 10) * Alignments - 55;
		if (Version >= 7)
			Result -= 36;
	}
	return Result;
}

int DataCodewords(int Version, QrLevel Level)
{
	return RawModules(Version) / 8 - ECC_PER_BLOCK[Level][Version] * ECC_BLOCKS[Level][Version];
}

// The bits byte mode takes for Count bytes: its mode, the count and the bytes
long NeededBits(int Version, size_t Count)
{
	return 4 + (Version <= 9 ? 8 : 16) + 8 * static_cast<long>(Count);
}

// Multiplication in GF(2^8) modulo x^8 + x^4 + x^3 + x^2 + 1
unsigned char Multiply(unsigned char a, unsigned char b)
{
	unsigned int Product = 0;
	for (int i = 7; i >= 0; --i) {
		Product = (Product << 1) ^ ((Product >> 7) * 0x11Du);
		Product ^= ((b >> i) & 1u) * a;
	}
	return static_cast<unsigned char>(Product);
}

// The Reed-Solomon generator polynomial of a degree, the product of
// (x - 2^i) for i below it: its coefficients from the highest power down,
// without the leading 1
std::vector<unsigned char> Generator(int Degree)
{
	std::vector<unsigned char> Result(static_cast<size_t>(Degree), 0);
	Result.back() = 1;
	unsigned char Root = 1;
	for (int i = 0; i < Degree; ++i) {
		for (size_t j = 0; j < Result.size(); ++j) {
			Result[j] = Multiply(Result[j], Root);
			if (j + 1 < Result.size())
				Result[j] ^= Result[j + 1];
		}
		Root = Multiply(Root, 0x02);
	}
	return Result;
}

// A block's error correction codewords: the remainder of its data divided by the generator
std::vector<unsigned char> Correction(const std::vector<unsigned char> &Data, const std::vector<unsigned char> &Divisor)
{
	std::vector<unsigned char> Result(Divisor.size(), 0);
	for (size_t i = 0; i < Data.size(); ++i) {
		const unsigned char Factor = static_cast<unsigned char>(Data[i] ^ Result[0]);
		Result.erase(Result.begin());
		Result.push_back(0);
		for (size_t j = 0; j < Divisor.size(); ++j)
			Result[j] ^= Multiply(Divisor[j], Factor);
	}
	return Result;
}

// The data codewords split into blocks, each with its error correction, and
// read across the blocks in turn as the symbol holds them
std::vector<unsigned char> Interleaved(const std::vector<unsigned char> &Data, int Version, QrLevel Level)
{
	const int Blocks = ECC_BLOCKS[Level][Version], Ecc = ECC_PER_BLOCK[Level][Version];
	const int Raw = RawModules(Version) / 8;
	const int Short = Blocks - Raw % Blocks, ShortLength = Raw / Blocks;
	const std::vector<unsigned char> Divisor = Generator(Ecc);
	std::vector<std::vector<unsigned char> > All;
	size_t At = 0;
	for (int b = 0; b < Blocks; ++b) {
		const size_t Length = static_cast<size_t>(ShortLength - Ecc + (b < Short ? 0 : 1));
		std::vector<unsigned char> Block(Data.begin() + static_cast<std::ptrdiff_t>(At),
			Data.begin() + static_cast<std::ptrdiff_t>(At + Length));
		At += Length;
		const std::vector<unsigned char> Check = Correction(Block, Divisor);
		if (b < Short)
			Block.push_back(0);		// a place the long blocks fill, skipped below
		Block.insert(Block.end(), Check.begin(), Check.end());
		All.push_back(Block);
	}
	std::vector<unsigned char> Result;
	for (size_t i = 0; i < All[0].size(); ++i)
		for (size_t b = 0; b < All.size(); ++b)
			if (i != static_cast<size_t>(ShortLength - Ecc) || static_cast<int>(b) >= Short)
				Result.push_back(All[b][i]);
	return Result;
}

class Grid {
public:
	explicit Grid(int Version) : m_iSize(17 + 4 * Version),
		m_Dark(static_cast<size_t>(m_iSize * m_iSize), 0), m_Function(static_cast<size_t>(m_iSize * m_iSize), 0) {}

	int Size() const { return m_iSize; }
	bool Dark(int x, int y) const { return m_Dark[Index(x, y)] != 0; }
	bool Function(int x, int y) const { return m_Function[Index(x, y)] != 0; }
	void Set(int x, int y, bool On) { m_Dark[Index(x, y)] = On ? 1 : 0; }
	void SetFunction(int x, int y, bool On) { Set(x, y, On); m_Function[Index(x, y)] = 1; }
	const std::vector<unsigned char> &Modules() const { return m_Dark; }

private:
	size_t Index(int x, int y) const { return static_cast<size_t>(y * m_iSize + x); }

	int m_iSize;
	std::vector<unsigned char> m_Dark, m_Function;
};

void DrawFinder(Grid &G, int cx, int cy)
{
	for (int dy = -4; dy <= 4; ++dy)
		for (int dx = -4; dx <= 4; ++dx) {
			const int x = cx + dx, y = cy + dy;
			if (x < 0 || x >= G.Size() || y < 0 || y >= G.Size())
				continue;
			const int Ring = std::max(std::abs(dx), std::abs(dy));	// the separator around it is light too
			G.SetFunction(x, y, Ring != 2 && Ring != 4);
		}
}

void DrawAlignment(Grid &G, int cx, int cy)
{
	for (int dy = -2; dy <= 2; ++dy)
		for (int dx = -2; dx <= 2; ++dx)
			G.SetFunction(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
}

// The rows and columns the alignment patterns are centred on
std::vector<int> AlignmentPlaces(int Version)
{
	std::vector<int> Result;
	if (Version == 1)
		return Result;
	const int Count = Version / 7 + 2, Size = 17 + 4 * Version;
	const int Step = (Version * 8 + Count * 3 + 5) / (Count * 4 - 4) * 2;
	for (int i = 0; i < Count - 1; ++i)
		Result.push_back(Size - 7 - i * Step);
	Result.push_back(6);
	std::reverse(Result.begin(), Result.end());
	return Result;
}

bool Bit(long Value, int i)
{
	return ((Value >> i) & 1) != 0;
}

// The level and mask, with their BCH code, beside the finders (twice)
void DrawFormat(Grid &G, QrLevel Level, int Mask)
{
	const int Data = LEVEL_BITS[Level] << 3 | Mask;
	int Rest = Data;
	for (int i = 0; i < 10; ++i)
		Rest = (Rest << 1) ^ ((Rest >> 9) * 0x537);
	const long Bits = (static_cast<long>(Data) << 10 | Rest) ^ 0x5412;
	const int Size = G.Size();
	for (int i = 0; i <= 5; ++i)
		G.SetFunction(8, i, Bit(Bits, i));
	G.SetFunction(8, 7, Bit(Bits, 6));
	G.SetFunction(8, 8, Bit(Bits, 7));
	G.SetFunction(7, 8, Bit(Bits, 8));
	for (int i = 9; i < 15; ++i)
		G.SetFunction(14 - i, 8, Bit(Bits, i));
	for (int i = 0; i < 8; ++i)
		G.SetFunction(Size - 1 - i, 8, Bit(Bits, i));
	for (int i = 8; i < 15; ++i)
		G.SetFunction(8, Size - 15 + i, Bit(Bits, i));
	G.SetFunction(8, Size - 8, true);	// always dark
}

// From version 7 on, the version with its BCH code, beside two finders
void DrawVersion(Grid &G, int Version)
{
	if (Version < 7)
		return;
	int Rest = Version;
	for (int i = 0; i < 12; ++i)
		Rest = (Rest << 1) ^ ((Rest >> 11) * 0x1F25);
	const long Bits = static_cast<long>(Version) << 12 | Rest;
	for (int i = 0; i < 18; ++i) {
		const int a = G.Size() - 11 + i % 3, b = i / 3;
		G.SetFunction(a, b, Bit(Bits, i));
		G.SetFunction(b, a, Bit(Bits, i));
	}
}

void DrawFunctionPatterns(Grid &G, int Version, QrLevel Level)
{
	const int Size = G.Size();
	for (int i = 0; i < Size; ++i) {
		G.SetFunction(6, i, i % 2 == 0);	// timing patterns
		G.SetFunction(i, 6, i % 2 == 0);
	}
	DrawFinder(G, 3, 3);
	DrawFinder(G, Size - 4, 3);
	DrawFinder(G, 3, Size - 4);
	const std::vector<int> Places = AlignmentPlaces(Version);
	const size_t Count = Places.size();
	for (size_t i = 0; i < Count; ++i)
		for (size_t j = 0; j < Count; ++j)
			if (!((i == 0 && j == 0) || (i == 0 && j == Count - 1) || (i == Count - 1 && j == 0)))	// not on the finders
				DrawAlignment(G, Places[i], Places[j]);
	DrawFormat(G, Level, 0);	// a place for it, filled in once the mask is known
	DrawVersion(G, Version);
}

// The codewords' bits from the bottom right corner, up and down two columns
// at a time, around the function patterns (the timing column left out)
void PlaceCodewords(Grid &G, const std::vector<unsigned char> &Codewords)
{
	const size_t Bits = Codewords.size() * 8;
	size_t i = 0;
	const int Size = G.Size();
	for (int Right = Size - 1; Right >= 1; Right -= 2) {
		if (Right == 6)
			Right = 5;
		const bool Upward = ((Right + 1) & 2) == 0;
		for (int Step = 0; Step < Size; ++Step)
			for (int j = 0; j < 2; ++j) {
				const int x = Right - j, y = Upward ? Size - 1 - Step : Step;
				if (!G.Function(x, y) && i < Bits) {
					G.Set(x, y, ((Codewords[i >> 3] >> (7 - static_cast<int>(i & 7))) & 1) != 0);
					++i;
				}
			}
	}
}

bool MaskHits(int Mask, int x, int y)
{
	switch (Mask) {
	case 0: return (x + y) % 2 == 0;
	case 1: return y % 2 == 0;
	case 2: return x % 3 == 0;
	case 3: return (x + y) % 3 == 0;
	case 4: return (x / 3 + y / 2) % 2 == 0;
	case 5: return x * y % 2 + x * y % 3 == 0;
	case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
	default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
	}
}

// Flips the data modules the mask hits (twice puts them back)
void ApplyMask(Grid &G, int Mask)
{
	for (int y = 0; y < G.Size(); ++y)
		for (int x = 0; x < G.Size(); ++x)
			if (!G.Function(x, y) && MaskHits(Mask, x, y))
				G.Set(x, y, !G.Dark(x, y));
}

// The runs of a row or column before the current one, the newest first, for
// spotting a finder's 1:1:3:1:1 with four light modules on one side; the
// light border around the symbol counts as part of the first and last runs
class Runs {
public:
	explicit Runs(int Size) : m_iSize(Size)
	{
		for (int i = 0; i < 7; ++i)
			m_Length[i] = 0;
	}

	void Add(int Length)
	{
		if (m_Length[0] == 0)
			Length += m_iSize;
		for (int i = 6; i > 0; --i)
			m_Length[i] = m_Length[i - 1];
		m_Length[0] = Length;
	}

	int Patterns() const
	{
		const int n = m_Length[1];
		const bool Core = n > 0 && m_Length[2] == n && m_Length[3] == n * 3 && m_Length[4] == n && m_Length[5] == n;
		return (Core && m_Length[0] >= n * 4 && m_Length[6] >= n ? 1 : 0) + (Core && m_Length[6] >= n * 4 && m_Length[0] >= n ? 1 : 0);
	}

	int End(bool Dark, int Length)
	{
		if (Dark) {
			Add(Length);
			Length = 0;
		}
		Add(Length + m_iSize);
		return Patterns();
	}

private:
	int m_iSize;
	int m_Length[7];
};

int Penalty(const Grid &G)
{
	const int Size = G.Size();
	int Result = 0;
	for (int Pass = 0; Pass < 2; ++Pass)	// the rows, then the columns
		for (int a = 0; a < Size; ++a) {
			bool Color = false;
			int Length = 0;
			Runs History(Size);
			for (int b = 0; b < Size; ++b) {
				const bool Dark = Pass == 0 ? G.Dark(b, a) : G.Dark(a, b);
				if (Dark == Color) {
					++Length;
					if (Length == 5)
						Result += PENALTY_RUN;
					else if (Length > 5)
						++Result;
				}
				else {
					History.Add(Length);
					if (!Color)
						Result += History.Patterns() * PENALTY_FINDER;
					Color = Dark;
					Length = 1;
				}
			}
			Result += History.End(Color, Length) * PENALTY_FINDER;
		}
	for (int y = 0; y + 1 < Size; ++y)
		for (int x = 0; x + 1 < Size; ++x) {
			const bool Dark = G.Dark(x, y);
			if (Dark == G.Dark(x + 1, y) && Dark == G.Dark(x, y + 1) && Dark == G.Dark(x + 1, y + 1))
				Result += PENALTY_BLOCK;
		}
	int Dark = 0;
	for (size_t i = 0; i < G.Modules().size(); ++i)
		Dark += G.Modules()[i] != 0 ? 1 : 0;
	const int Total = Size * Size;
	// The smallest k with 45 - 5k % <= dark <= 55 + 5k %
	const int k = (std::abs(Dark * 20 - Total * 10) + Total - 1) / Total - 1;
	return Result + k * PENALTY_BALANCE;
}

}	// namespace

bool QrSymbol::Dark(int x, int y) const
{
	return x >= 0 && y >= 0 && x < Size && y < Size && Modules[static_cast<size_t>(y * Size + x)] != 0;
}

bool MakeQrSymbol(const std::string &Bytes, QrLevel Level, QrSymbol &Out, int Mask, bool Boost, int MinVersion)
{
	if (Level < QR_LOW || Level > QR_HIGH || Mask < -1 || Mask > 7 || MinVersion > 40)
		return false;
	int Version = 0;
	for (int v = std::max(1, MinVersion); v <= 40 && Version == 0; ++v)
		if (NeededBits(v, Bytes.size()) <= DataCodewords(v, Level) * 8L)
			Version = v;
	if (Version == 0)
		return false;
	for (int Higher = Level + 1; Boost && Higher <= QR_HIGH; ++Higher)
		if (NeededBits(Version, Bytes.size()) <= DataCodewords(Version, static_cast<QrLevel>(Higher)) * 8L)
			Level = static_cast<QrLevel>(Higher);

	// Byte mode: 0100, the count, the bytes, up to four zero bits, then zero
	// bits to a whole byte and the pad bytes 11101100 and 00010001 in turn
	const long Capacity = DataCodewords(Version, Level) * 8L;
	std::vector<bool> Bits;
	const long Count = static_cast<long>(Bytes.size());
	for (int i = 3; i >= 0; --i)
		Bits.push_back(Bit(4, i));
	for (int i = (Version <= 9 ? 8 : 16) - 1; i >= 0; --i)
		Bits.push_back(Bit(Count, i));
	for (size_t b = 0; b < Bytes.size(); ++b)
		for (int i = 7; i >= 0; --i)
			Bits.push_back(Bit(static_cast<unsigned char>(Bytes[b]), i));
	for (int i = 0; i < 4 && static_cast<long>(Bits.size()) < Capacity; ++i)
		Bits.push_back(false);
	while (Bits.size() % 8 != 0)
		Bits.push_back(false);
	for (unsigned char Pad = 0xEC; static_cast<long>(Bits.size()) < Capacity; Pad ^= 0xEC ^ 0x11)
		for (int i = 7; i >= 0; --i)
			Bits.push_back(Bit(Pad, i));
	std::vector<unsigned char> Data(Bits.size() / 8, 0);
	for (size_t i = 0; i < Bits.size(); ++i)
		if (Bits[i])
			Data[i >> 3] = static_cast<unsigned char>(Data[i >> 3] | (0x80 >> (i & 7)));

	Grid G(Version);
	DrawFunctionPatterns(G, Version, Level);
	PlaceCodewords(G, Interleaved(Data, Version, Level));
	if (Mask < 0) {
		int Lowest = 0;
		for (int m = 0; m < 8; ++m) {
			ApplyMask(G, m);
			DrawFormat(G, Level, m);
			const int Score = Penalty(G);
			if (m == 0 || Score < Lowest) {
				Lowest = Score;
				Mask = m;
			}
			ApplyMask(G, m);
		}
	}
	ApplyMask(G, Mask);
	DrawFormat(G, Level, Mask);

	Out.Version = Version;
	Out.Size = G.Size();
	Out.Level = Level;
	Out.Mask = Mask;
	Out.Modules = G.Modules();
	return true;
}

}	// namespace ftapi

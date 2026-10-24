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

#include "FtmFile.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>

namespace nsf {

namespace {

// FamiTracker 0.4.6's limits (FamiTrackerTypes.h, Instrument.h, DSample.h)
const int MAX_INSTRUMENTS = 64;
const int MAX_SEQUENCES = 128;
const int MAX_SEQUENCE_ITEMS = 253;
const int MAX_PATTERN = 128;
const int MAX_FRAMES = 128;
const int MAX_PATTERN_LENGTH = 256;
const int MAX_DSAMPLES = 64;
const unsigned MAX_TRACKS = 64;
const unsigned MAX_TEMPO = 255;
const unsigned MAX_CHANNELS = 28;		// the most channels a track keeps patterns for
const int OCTAVES = 8;
const unsigned INST_NAME_MAX = 128;
const int SAMPLE_NAME_MAX = 256;
const int SAMPLE_SIZE_MAX = 0x8000;		// the most ReadBlock_DSamples takes
const int FDS_WAVE_SIZE = 64, FDS_MOD_SIZE = 32;
const int N163_WAVE_SIZE = 32, N163_WAVES = 16;
const int STRING_MAX = 65536;			// the most characters ReadString keeps

// The file (CDocumentFile)
const char FILE_ID[] = "FamiTracker Module";
const size_t FILE_ID_SIZE = 18;
const unsigned FILE_VER = 0x0440;			// 0.4.6's (4.40)
const unsigned BLOCKS_VER = 0x0200;			// the first made of blocks (2.00)
const uint32_t MAX_BLOCK = 50000000;		// a bigger block means a broken file
const size_t MAX_FILE = size_t(64) << 20;

// The most cells the tracks are read into (about 370 MB): a byte of a frame
// list asks for a pattern of up to 256 rows, and a module has up to 64
// tracks. Real modules have tens of thousands.
const long MAX_CELLS = 1L << 23;

// The most problems listed
const size_t MAX_PROBLEMS = 64;

// Defaults (FamiTrackerDoc.h)
const unsigned NTSC = 0, PAL = 1;
const unsigned DEFAULT_TEMPO_NTSC = 150, DEFAULT_TEMPO_PAL = 125, DEFAULT_SPEED = 6, DEFAULT_ROWS = 64;
const unsigned DEFAULT_SPLIT_POINT = 32, OLD_SPLIT_POINT = 21;
const unsigned DEFAULT_NAMCO_CHANNELS = 1;

const unsigned NO_POINT = 0xFFFFFFFFu;		// a sequence without a loop or release point (-1)

// FamiTracker's instrument types (inst_type_t)
enum { TYPE_2A03 = 1, TYPE_VRC6, TYPE_VRC7, TYPE_FDS, TYPE_N163, TYPE_S5B };

// The effects (effect_t) that loading changes
enum { EF_SPEED = 1, EF_PORTAMENTO = 6, EF_PORTAOFF = 7, EF_PITCH = 13, EF_PORTA_UP = 16, EF_PORTA_DOWN = 17 };

// The notes (note_t) besides C-B (1-12)
enum { FT_RELEASE = 13, FT_HALT = 14 };

// An empty cell's instrument and volume
const int NO_INSTRUMENT = 0x40, NO_VOLUME = 0x10;

// The FDS channel's ID (chan_id_t)
const int CHANID_FDS = 19;

// The letters of effects 1-32 as FamiTracker writes them (EFF_CHAR): the FDS's
// and the 5B's own effects reuse H, I and J; 0 for the two without one
const char EFF_CHAR[32] = {
	'F', 'B', 'D', 'C', 'E', '3', 0, 'H', 'I', '0', '4', '7', 'P', 'G', 'Z', '1',
	'2', 'V', 'Y', 'Q', 'R', 'A', 'S', 'X', 0, 'H', 'I', 'J', 'W', 'H', 'I', 'J'
};

// What 0.4.6 writes of each block, to tell newer ones
struct BlockKind {
	const char *Id;
	int Version;
};
const BlockKind BLOCKS[] = {
	{ "PARAMS", 6 }, { "INFO", 1 }, { "HEADER", 3 }, { "INSTRUMENTS", 6 }, { "SEQUENCES", 6 }, { "FRAMES", 3 },
	{ "PATTERNS", 5 }, { "DPCM SAMPLES", 1 }, { "COMMENTS", 1 }, { "SEQUENCES_VRC6", 6 }, { "SEQUENCES_N163", 1 },
	{ "SEQUENCES_N106", 1 }, { "SEQUENCES_S5B", 1 }
};

// Bytes and words as the signed values FamiTracker reads them as (char is
// signed with Visual C++)
int SignedByte(unsigned v)
{
	v &= 0xFF;
	return v < 0x80 ? int(v) : int(v) - 0x100;
}

int SignedWord(uint32_t v)
{
	return v < 0x80000000u ? int(v) : -int(~v) - 1;
}

int8_t ToChar(int v)
{
	return int8_t(SignedByte(unsigned(v)));
}

uint32_t Word(const uint8_t *p)
{
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

std::string Number(long v)
{
	char Buffer[24];
	std::snprintf(Buffer, sizeof(Buffer), "%ld", v);
	return Buffer;
}

// A block's name to show: printable characters, others as '?'
std::string Shown(const std::string &Id)
{
	std::string s;
	for (size_t i = 0; i < Id.size(); ++i)
		s += Id[i] >= 0x20 && Id[i] < 0x7F ? Id[i] : '?';
	return s.empty() ? std::string("unnamed") : s;
}

// FamiTracker's text is in the Windows code page; read as Windows-1252, the
// Western one, into UTF-8 (the five codes it leaves out as the control
// characters of the same numbers, as Windows does)
std::string FromWindows(const std::string &s)
{
	static const unsigned short HIGH[32] = {
		0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
		0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
	};
	std::string r;
	for (size_t i = 0; i < s.size(); ++i) {
		const unsigned c = static_cast<unsigned char>(s[i]);
		if (c < 0x80) {
			r += char(c);
			continue;
		}
		const unsigned Code = c < 0xA0 ? HIGH[c - 0x80] : c;
		if (Code < 0x800) {
			r += char(0xC0 | (Code >> 6));
			r += char(0x80 | (Code & 0x3F));
		}
		else {
			r += char(0xE0 | (Code >> 12));
			r += char(0x80 | ((Code >> 6) & 0x3F));
			r += char(0x80 | (Code & 0x3F));
		}
	}
	return r;
}

// A block's data, read as CDocumentFile reads it: ints little-endian, chars
// signed. Each read is checked: past the end reads zero and marks the block
// cut short, where 0.4.6 reads on into whatever memory follows.
class BlockReader {
public:
	BlockReader(const uint8_t *Data, size_t Size, int Version) : m_pData(Data), m_Size(Size), m_Pos(0), m_Version(Version), m_Short(false) {}

	int Version() const { return m_Version; }
	bool Short() const { return m_Short; }
	bool Done() const { return m_Pos >= m_Size; }

	int Int()
	{
		if (!Has(4))
			return 0;
		const uint32_t v = Word(m_pData + m_Pos);
		m_Pos += 4;
		return SignedWord(v);
	}
	unsigned UInt() { return unsigned(Int()); }

	int Char()
	{
		if (!Has(1))
			return 0;
		return SignedByte(m_pData[m_Pos++]);
	}
	unsigned UChar() { return unsigned(Char()) & 0xFF; }

	// The next two ints, left to be read (CInstrumentFDS::Load looks ahead);
	// false when they aren't there
	bool Peek(uint32_t &a, uint32_t &b) const
	{
		if (m_Size - m_Pos < 8)
			return false;
		a = Word(m_pData + m_Pos);
		b = Word(m_pData + m_Pos + 4);
		return true;
	}

	// Size bytes as a C string (GetBlock into a buffer that ends with a NUL)
	std::string Text(size_t Size)
	{
		const std::string s = Raw(Size);
		return s.substr(0, std::min(s.size(), std::strlen(s.c_str())));
	}

	std::vector<uint8_t> Bytes(size_t Size)
	{
		std::vector<uint8_t> v;
		if (Has(Size)) {
			v.assign(m_pData + m_Pos, m_pData + m_Pos + Size);
			m_Pos += Size;
		}
		return v;
	}

	// ReadString: up to a NUL; 0.4.6 keeps 65536 characters and stops after
	// swallowing one more
	std::string String()
	{
		std::string s;
		int Count = 0;
		for (;;) {
			const int c = Char();
			if (c == 0 || m_Short)
				break;
			if (Count++ >= STRING_MAX)
				break;
			s += char(c);
		}
		return s;
	}

private:
	bool Has(size_t n)
	{
		if (m_Short || n > m_Size - m_Pos) {
			m_Short = true;
			m_Pos = m_Size;
			return false;
		}
		return true;
	}

	std::string Raw(size_t Size)
	{
		if (!Has(Size))
			return std::string();
		const std::string s(reinterpret_cast<const char *>(m_pData + m_Pos), Size);
		m_Pos += Size;
		return s;
	}

	const uint8_t *m_pData;
	size_t m_Size, m_Pos;
	int m_Version;
	bool m_Short;
};

// A sequence as CSequence keeps it, with its setters' rules: a loop point
// not before the release point goes, and so do points past the items
struct Sequence {
	unsigned Count, Loop, Release, Setting;
	int8_t Items[MAX_SEQUENCE_ITEMS];

	Sequence() { Clear(); }
	void Clear()
	{
		Count = 0;
		Loop = Release = NO_POINT;
		Setting = 0;
		std::memset(Items, 0, sizeof(Items));
	}
	// FamiTracker keeps up to 253 items (with more, the ones past them are
	// whatever memory is there)
	void SetCount(unsigned n)
	{
		Count = std::min(n, unsigned(MAX_SEQUENCE_ITEMS));
		if (Loop > Count)
			Loop = NO_POINT;
		if (Release > Count)
			Release = NO_POINT;
	}
	void SetLoop(unsigned Point)
	{
		Loop = Point;
		if (Loop >= Release)
			Loop = NO_POINT;
	}
	void SetRelease(unsigned Point)
	{
		Release = Point;
		if (Loop >= Release)
			Loop = NO_POINT;
	}
	void SetItem(unsigned i, int Value)
	{
		if (i < unsigned(MAX_SEQUENCE_ITEMS))
			Items[i] = ToChar(Value);
	}
};

// A sequence of the first file versions (stSequence): a value and the frames
// it lasts less one, a negative length looping back over that many entries
struct OldSequence {
	unsigned Count;
	int8_t Length[MAX_SEQUENCE_ITEMS], Value[MAX_SEQUENCE_ITEMS];
	OldSequence() : Count(0)
	{
		std::memset(Length, 0, sizeof(Length));
		std::memset(Value, 0, sizeof(Value));
	}
};

struct FtInstrument {
	int Type;					// 0 for an empty slot
	std::string Name;
	int SeqEnable[SEQ_TYPES], SeqIndex[SEQ_TYPES];		// 2A03, VRC6, N163
	int Samples[OCTAVES][12], Pitches[OCTAVES][12], Deltas[OCTAVES][12];	// 2A03's DPCM keys
	unsigned Patch, Regs[8];							// VRC7
	int Wave[FDS_WAVE_SIZE], Mod[FDS_MOD_SIZE];			// FDS
	int ModSpeed, ModDepth, ModDelay;
	Sequence Fds[3];			// the FDS's volume, arpeggio and pitch
	int WaveSize, WavePos, WaveCount;					// N163
	int Waves[N163_WAVES][N163_WAVE_SIZE];

	FtInstrument() : Type(0), Patch(0), ModSpeed(0), ModDepth(0), ModDelay(0), WaveSize(32), WavePos(0), WaveCount(1)
	{
		static const unsigned VRC7_REGS[8] = { 0x01, 0x21, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x0F };
		for (int t = 0; t < SEQ_TYPES; ++t)
			SeqEnable[t] = SeqIndex[t] = 0;
		for (int o = 0; o < OCTAVES; ++o)
			for (int n = 0; n < 12; ++n) {
				Samples[o][n] = Pitches[o][n] = 0;
				Deltas[o][n] = -1;
			}
		for (int i = 0; i < 8; ++i)
			Regs[i] = VRC7_REGS[i];
		std::memset(Wave, 0, sizeof(Wave));
		std::memset(Mod, 0, sizeof(Mod));
		std::memset(Waves, 0, sizeof(Waves));
	}
};

// A row of a pattern (stChanNote)
struct FtNote {
	uint8_t Note, Octave, Vol, Instrument;
	uint8_t EffNumber[MAX_EFFECTS], EffParam[MAX_EFFECTS];

	FtNote() : Note(0), Octave(0), Vol(0), Instrument(0)
	{
		std::memset(EffNumber, 0, sizeof(EffNumber));
		std::memset(EffParam, 0, sizeof(EffParam));
	}
	// Nothing in it (CPatternData::IsCellFree)
	bool Free() const
	{
		return Note == 0 && EffNumber[0] == 0 && EffNumber[1] == 0 && EffNumber[2] == 0 && EffNumber[3] == 0 &&
			Vol == NO_VOLUME && Instrument == NO_INSTRUMENT;
	}
};

// A track (CPatternData)
struct FtTrack {
	bool Allocated;
	unsigned PatternLength, FrameCount, Speed, Tempo;
	std::string Name;
	uint8_t Columns[MAX_CHANNELS];				// effect columns less one
	uint8_t Frames[MAX_FRAMES][MAX_CHANNELS];
	std::map<int, std::map<int, FtNote> > Rows;	// the rows written, by channel * MAX_PATTERN + pattern and row

	FtTrack() : Allocated(false), PatternLength(DEFAULT_ROWS), FrameCount(1), Speed(DEFAULT_SPEED), Tempo(DEFAULT_TEMPO_NTSC)
	{
		std::memset(Columns, 0, sizeof(Columns));
		std::memset(Frames, 0, sizeof(Frames));
	}
};

// The channels FamiTracker has for the chips that are on (bits), as
// chan_id_t, in its order (CSoundGen::RegisterChannels): the 2A03's, then
// every chip's, the N163's it uses among them (Namco), then the N163's past
// those. With one chip on, 0.4.6's order.
std::vector<int> ChannelsOf(unsigned Chip, unsigned Namco)
{
	std::vector<int> c;
	for (int i = 0; i < 5; ++i)
		c.push_back(i);
	if (Chip & EXP_VRC6)
		for (int i = 5; i < 8; ++i)
			c.push_back(i);
	if (Chip & EXP_MMC5)
		for (int i = 8; i < 10; ++i)
			c.push_back(i);
	if (Chip & EXP_N163)
		for (int i = 11; i < 11 + int(std::min(Namco, 8u)); ++i)
			c.push_back(i);
	if (Chip & EXP_FDS)
		c.push_back(CHANID_FDS);
	if (Chip & EXP_VRC7)
		for (int i = 20; i < 26; ++i)
			c.push_back(i);
	if (Chip & EXP_S5B)
		for (int i = 26; i < 29; ++i)
			c.push_back(i);
	if (Chip & EXP_N163)
		for (int i = 11 + int(std::min(Namco, 8u)); i < 19; ++i)
			c.push_back(i);
	return c;
}

bool Fail(std::string &Why, const std::string &What)
{
	Why = What;
	return false;
}

// The module as FamiTracker 0.4.6 loads it (CFamiTrackerDoc after
// DeleteContents, in an application just started: the 2A03's channels, one
// N163 channel)
class Document {
public:
	Document(unsigned FileVersion, std::vector<std::string> &Problems) :
		m_FileVersion(FileVersion), m_Problems(Problems), m_Chip(0), m_ChannelsAvailable(5), m_Machine(NTSC), m_EngineSpeed(0),
		m_Vibrato(0), m_SplitPoint(DEFAULT_SPLIT_POINT), m_NamcoChannels(DEFAULT_NAMCO_CHANNELS), m_FirstHighlight(4),
		m_SecondHighlight(16), m_TrackCount(1), m_Tracks(MAX_TRACKS), m_Instruments(MAX_INSTRUMENTS),
		m_Sequences(MACRO_CHIPS * MAX_SEQUENCES * SEQ_TYPES), m_Samples(MAX_DSAMPLES), m_SampleNames(MAX_DSAMPLES),
		m_AdjustFdsArpeggio(false), m_HighNotes(0), m_UnknownEffects(0)
	{
		m_ChannelTypes = ChannelsOf(0, 0);
		// Modules from before 2.10 have one track, made first
		if (m_FileVersion < 0x0210)
			Track(0);
	}

	bool ReadBlocks(const std::vector<uint8_t> &Bytes, std::string &Error);
	void Convert();
	bool MakeModule(Module &Out, FtmInfo &Info, std::string &Error);

private:
	bool ReadBlock(const std::string &Id, BlockReader &b, std::string &Error);
	bool ReadParams(BlockReader &b, std::string &Why);
	bool ReadInfo(BlockReader &b, std::string &Why);
	bool ReadHeader(BlockReader &b, std::string &Why);
	bool ReadInstruments(BlockReader &b, std::string &Why);
	bool LoadInstrument(BlockReader &b, int Slot, FtInstrument &Inst, std::string &Why);
	bool LoadFdsSequence(BlockReader &b, Sequence &Seq, std::string &Why);
	bool ReadSequences(BlockReader &b, std::string &Why);
	bool ReadSequencesVrc6(BlockReader &b, std::string &Why);
	bool ReadSequencesN163(BlockReader &b, std::string &Why, int Chip);	// the N163's or the 5B's (the same block)
	bool ReadFrames(BlockReader &b, std::string &Why);
	bool ReadPatterns(BlockReader &b, std::string &Why);
	bool ReadSamples(BlockReader &b, std::string &Why);
	bool ReadComments(BlockReader &b, std::string &Why);

	void SetupChannels(unsigned Chip);
	FtTrack &Track(unsigned Index);
	Sequence &Seq(int Chip, int Index, int Type) { return m_Sequences[(Chip * MAX_SEQUENCES + Index) * SEQ_TYPES + Type]; }
	int ChannelType(unsigned Channel) const { return Channel < m_ChannelTypes.size() ? m_ChannelTypes[Channel] : -1; }
	void ReorderSequences();
	void ConvertSequences();
	void Problem(const std::string &What);
	Cell MakeCell(const FtNote &n, bool Noise);

	const unsigned m_FileVersion;
	std::vector<std::string> &m_Problems;

	unsigned m_Chip, m_ChannelsAvailable, m_Machine, m_EngineSpeed;
	int m_Vibrato;
	unsigned m_SplitPoint, m_NamcoChannels;
	int m_FirstHighlight, m_SecondHighlight;
	std::vector<int> m_ChannelTypes;		// the channels registered for the chip
	std::string m_Title, m_Author, m_Copyright, m_Comment;
	unsigned m_TrackCount;
	std::vector<FtTrack> m_Tracks;
	std::vector<FtInstrument> m_Instruments;
	std::vector<Sequence> m_Sequences;		// [chip: 2A03, VRC6, N163, 5B (macro_chip)][index][type]
	std::vector<OldSequence> m_TmpSequences;			// SEQUENCES 1: by index alone
	std::vector<std::vector<OldSequence> > m_OldSequences;	// SEQUENCES 2, and 1 reordered: [index][type]
	std::vector<std::vector<uint8_t> > m_Samples;
	std::vector<std::string> m_SampleNames;
	bool m_AdjustFdsArpeggio;
	long m_HighNotes, m_UnknownEffects;		// left out of the cells
};

void Document::Problem(const std::string &What)
{
	if (std::find(m_Problems.begin(), m_Problems.end(), What) != m_Problems.end())
		return;
	if (m_Problems.size() < MAX_PROBLEMS)
		m_Problems.push_back(What);
	else if (m_Problems.size() == MAX_PROBLEMS)
		m_Problems.push_back("and more problems than these");
}

FtTrack &Document::Track(unsigned Index)
{
	// Made when first used (CFamiTrackerDoc::AllocateTrack), with the tempo of the machine set then
	FtTrack &T = m_Tracks[Index];
	if (!T.Allocated) {
		T.Allocated = true;
		T.Tempo = m_Machine == NTSC ? DEFAULT_TEMPO_NTSC : DEFAULT_TEMPO_PAL;
		T.Name = "New song";
	}
	return T;
}

void Document::SetupChannels(unsigned Chip)
{
	// Expansion chips play at NTSC's rate
	if (Chip != 0)
		m_Machine = NTSC;
	m_Chip = Chip;
	m_ChannelTypes = ChannelsOf(Chip, m_NamcoChannels);
	m_ChannelsAvailable = unsigned(m_ChannelTypes.size());
	if (Chip & EXP_N163)
		m_ChannelsAvailable -= 8 - m_NamcoChannels;
}

bool Document::ReadBlocks(const std::vector<uint8_t> &Bytes, std::string &Error)
{
	size_t Pos = FILE_ID_SIZE + 4;
	for (;;) {
		if (Pos >= Bytes.size()) {
			// FamiTracker takes a file that ends between blocks
			Problem("the file ends without its END mark");
			return true;
		}
		// The block's name, NUL-padded to 16 bytes; END is 3 bytes and the file's end
		std::string Id;
		for (size_t i = 0; i < 16 && Pos + i < Bytes.size() && Bytes[Pos + i] != 0; ++i)
			Id += char(Bytes[Pos + i]);
		if (Id == "END")
			return true;
		if (Bytes.size() - Pos < 24) {
			Error = "the file is cut short in the " + Shown(Id) + " block's header";
			return false;
		}
		const int Version = SignedWord(Word(&Bytes[Pos + 16]));
		const uint32_t Size = Word(&Bytes[Pos + 20]);
		Pos += 24;
		if (Size > MAX_BLOCK) {
			Error = "the " + Shown(Id) + " block is too big to be right (" + Number(long(Size)) + " bytes)";
			return false;
		}
		if (Size > Bytes.size() - Pos) {
			Error = "the file is cut short in the " + Shown(Id) + " block";
			return false;
		}
		BlockReader b(Bytes.data() + Pos, Size, Version);
		Pos += Size;
		if (!ReadBlock(Id, b, Error))
			return false;
	}
}

bool Document::ReadBlock(const std::string &Id, BlockReader &b, std::string &Error)
{
	std::string Why;
	bool Ok;
	if (Id == "PARAMS")
		Ok = ReadParams(b, Why);
	else if (Id == "INFO")
		Ok = ReadInfo(b, Why);
	else if (Id == "INSTRUMENTS")
		Ok = ReadInstruments(b, Why);
	else if (Id == "SEQUENCES")
		Ok = ReadSequences(b, Why);
	else if (Id == "FRAMES")
		Ok = ReadFrames(b, Why);
	else if (Id == "PATTERNS")
		Ok = ReadPatterns(b, Why);
	else if (Id == "DPCM SAMPLES")
		Ok = ReadSamples(b, Why);
	else if (Id == "HEADER")
		Ok = ReadHeader(b, Why);
	else if (Id == "COMMENTS")
		Ok = ReadComments(b, Why);
	else if (Id == "SEQUENCES_VRC6")
		Ok = ReadSequencesVrc6(b, Why);
	else if (Id == "SEQUENCES_N163" || Id == "SEQUENCES_N106")	// its name before 0.4.0
		Ok = ReadSequencesN163(b, Why, MACROS_N163);
	else if (Id == "SEQUENCES_S5B")
		Ok = ReadSequencesN163(b, Why, MACROS_S5B);
	else if (Id == "BF CLIPS")
		return true;	// beginner-friendly FamiTracker's clips (Clips.cpp): copies of what the patterns hold
	else {
		Problem("skipped the block \"" + Shown(Id) + "\", which FamiTracker 0.4.6 doesn't read");
		return true;
	}
	if (b.Short()) {
		Error = "the " + Id + " block is cut short";
		return false;
	}
	if (!Ok) {
		Error = "the " + Id + " block is damaged: " + Why;
		return false;
	}
	for (size_t i = 0; i < sizeof(BLOCKS) / sizeof(BLOCKS[0]); ++i)
		if (Id == BLOCKS[i].Id && unsigned(b.Version()) > unsigned(BLOCKS[i].Version))
			Problem("the " + Id + " block is version " + Number(long(unsigned(b.Version()))) + ", newer than FamiTracker 0.4.6 writes (" +
				Number(BLOCKS[i].Version) + "): read as 0.4.6 reads it");
	return true;
}

bool Document::ReadParams(BlockReader &b, std::string &Why)
{
	const int Version = b.Version();
	FtTrack &First = Track(0);

	if (Version == 1)
		First.Speed = b.UInt();
	else
		m_Chip = b.UChar();
	m_ChannelsAvailable = b.UInt();
	m_Machine = b.UInt();
	m_EngineSpeed = b.UInt();
	if (m_Machine != NTSC && m_Machine != PAL)
		return Fail(Why, "the machine is " + Number(SignedWord(m_Machine)) + ", neither NTSC (0) nor PAL (1)");
	if (m_ChannelsAvailable > MAX_CHANNELS)	// every chip on has 28
		return Fail(Why, "it has " + Number(SignedWord(m_ChannelsAvailable)) + " channels");

	m_Vibrato = Version > 2 ? b.Int() : 0;
	m_FirstHighlight = 4;
	m_SecondHighlight = 16;
	if (Version > 3) {
		m_FirstHighlight = b.Int();
		m_SecondHighlight = b.Int();
	}

	// Some files say 0xFF for the chip with the 2A03's channels alone
	if (m_ChannelsAvailable == 5)
		m_Chip = 0;

	// 2.00's speeds were one less
	if (m_FileVersion == 0x0200 && int(First.Speed) < 20)
		++First.Speed;
	// The first versions kept the tempo as a speed of 20 or more
	if (Version == 1) {
		if (First.Speed > 19) {
			First.Tempo = First.Speed;
			First.Speed = DEFAULT_SPEED;
		}
		else
			First.Tempo = m_Machine == NTSC ? DEFAULT_TEMPO_NTSC : DEFAULT_TEMPO_PAL;
	}

	if (Version >= 5 && (m_Chip & EXP_N163)) {
		m_NamcoChannels = b.UInt();
		if (m_NamcoChannels >= 9)
			return Fail(Why, "it has " + Number(SignedWord(m_NamcoChannels)) + " N163 channels");
	}

	// Modules from before the setting set the speed with Fxx under 21
	m_SplitPoint = Version >= 6 ? b.UInt() : OLD_SPLIT_POINT;

	SetupChannels(m_Chip);
	return true;
}

bool Document::ReadInfo(BlockReader &b, std::string &)
{
	m_Title = b.Text(32);
	m_Author = b.Text(32);
	m_Copyright = b.Text(32);
	return true;
}

bool Document::ReadHeader(BlockReader &b, std::string &Why)
{
	const int Version = b.Version();
	if (Version == 1) {
		m_TrackCount = 1;
		FtTrack &T = Track(0);
		for (unsigned i = 0; i < m_ChannelsAvailable; ++i) {
			b.Char();		// the channel's type, not used
			T.Columns[i] = uint8_t(b.UChar());
		}
	}
	else if (Version >= 2) {
		// 0 means one track
		m_TrackCount = unsigned(b.Char() + 1);
		if (m_TrackCount > MAX_TRACKS)
			return Fail(Why, "it has " + Number(SignedWord(m_TrackCount)) + " tracks");
		if (m_TrackCount == 0)
			return Fail(Why, "it has no tracks");		// 0.4.6 goes on without one
		for (unsigned i = 0; i < m_TrackCount; ++i)
			Track(i);
		if (Version >= 3)
			for (unsigned i = 0; i < m_TrackCount; ++i)
				m_Tracks[i].Name = b.String();
		for (unsigned i = 0; i < m_ChannelsAvailable; ++i) {
			b.UChar();		// the channel's type, not used
			for (unsigned j = 0; j < m_TrackCount; ++j)
				m_Tracks[j].Columns[i] = uint8_t(b.UChar());
		}
		// Version 4 would have each track's highlight: 0.4.6 gives them all the module's
	}
	return true;
}

bool Document::ReadComments(BlockReader &b, std::string &)
{
	b.Int();		// shown when the module opens
	m_Comment = b.String();
	return true;
}

bool Document::ReadInstruments(BlockReader &b, std::string &Why)
{
	const int Count = b.Int();
	if (Count > MAX_INSTRUMENTS)
		return Fail(Why, "it has " + Number(Count) + " instruments");
	for (int i = 0; i < Count && !b.Short(); ++i) {
		const int Index = b.Int();
		if (Index < 0 || Index >= MAX_INSTRUMENTS)
			return Fail(Why, "it has an instrument numbered " + Number(Index));
		const int Type = b.Char();
		if (Type < TYPE_2A03 || Type > TYPE_S5B)
			return Fail(Why, "instrument " + Number(Index) + " is of a kind FamiTracker doesn't know (" + Number(Type) + ")");
		FtInstrument Inst;
		Inst.Type = Type;
		if (!LoadInstrument(b, Index, Inst, Why))
			return false;
		const unsigned Size = b.UInt();
		if (Size > INST_NAME_MAX)
			return Fail(Why, "instrument " + Number(Index) + "'s name is " + Number(SignedWord(Size)) + " bytes long");
		Inst.Name = b.Text(Size);
		m_Instruments[Index] = Inst;
	}
	return true;
}

bool Document::LoadInstrument(BlockReader &b, int Slot, FtInstrument &Inst, std::string &Why)
{
	const int Version = b.Version();
	switch (Inst.Type) {
		case TYPE_2A03:
		case TYPE_VRC6:
		case TYPE_N163:
		case TYPE_S5B: {
			const int SeqCount = b.Int();
			if (SeqCount > SEQ_TYPES)
				return Fail(Why, "instrument " + Number(Slot) + " has " + Number(SeqCount) + " kinds of sequences");
			// The VRC6's, the N163's and the 5B's read all five
			const int Read = Inst.Type == TYPE_2A03 ? SeqCount : SEQ_TYPES;
			for (int i = 0; i < Read; ++i) {
				Inst.SeqEnable[i] = b.Char();
				Inst.SeqIndex[i] = b.Char();
				if (Inst.SeqEnable[i] && Inst.SeqIndex[i] < 0) {
					// 0.4.6 would look before its list
					Problem("instrument " + Number(Slot) + " plays a sequence numbered " + Number(Inst.SeqIndex[i]) + "; left out");
					Inst.SeqEnable[i] = 0;
				}
			}
			if (Inst.Type == TYPE_2A03) {
				// The DPCM keys: 6 octaves at first, a starting delta from version 6
				const int Octaves = Version == 1 ? 6 : OCTAVES;
				for (int o = 0; o < Octaves; ++o)
					for (int n = 0; n < 12; ++n) {
						int Sample = b.Char();
						if (Sample > MAX_DSAMPLES)
							Sample = 0;
						if (Sample < 0) {
							Problem("instrument " + Number(Slot) + " has a key with sample " + Number(Sample) + "; left out");
							Sample = 0;
						}
						Inst.Samples[o][n] = Sample;
						Inst.Pitches[o][n] = b.Char();
						if (Version > 5) {
							const int Delta = b.Char();
							Inst.Deltas[o][n] = Delta < 0 ? -1 : Delta;
						}
					}
			}
			else if (Inst.Type == TYPE_N163) {
				Inst.WaveSize = b.Int();
				if (Inst.WaveSize < 0 || Inst.WaveSize > N163_WAVE_SIZE)
					return Fail(Why, "instrument " + Number(Slot) + "'s waves are " + Number(Inst.WaveSize) + " samples long");
				Inst.WavePos = b.Int();
				if (Inst.WavePos < 0 || Inst.WavePos >= 256)	// as this build's loader (0.4.6's refused 128 and above)
					return Fail(Why, "instrument " + Number(Slot) + "'s waves go at " + Number(Inst.WavePos));
				Inst.WaveCount = b.Int();
				if (Inst.WaveCount < 1 || Inst.WaveCount > N163_WAVES)
					return Fail(Why, "instrument " + Number(Slot) + " has " + Number(Inst.WaveCount) + " waves");
				for (int w = 0; w < Inst.WaveCount; ++w)
					for (int s = 0; s < Inst.WaveSize; ++s) {
						const unsigned Sample = b.UChar();
						if (Sample >= 16 && !b.Short())
							return Fail(Why, "instrument " + Number(Slot) + "'s waves have a sample of " + Number(long(Sample)));
						Inst.Waves[w][s] = int(Sample);
					}
			}
			return true;
		}
		case TYPE_VRC7:
			Inst.Patch = b.UInt();
			for (int i = 0; i < 8; ++i)
				Inst.Regs[i] = unsigned(b.Char());
			return true;
		case TYPE_FDS: {
			for (int i = 0; i < FDS_WAVE_SIZE; ++i)
				Inst.Wave[i] = int(b.UChar());
			for (int i = 0; i < FDS_MOD_SIZE; ++i)
				Inst.Mod[i] = int(b.UChar());
			Inst.ModSpeed = b.Int();
			Inst.ModDepth = b.Int();
			Inst.ModDelay = b.Int();
			// The first FDS instruments had no sequences: their name's length
			// and its first letters follow (where a sequence's count and loop
			// point would). With less than 8 bytes left there's no room for
			// sequences either.
			uint32_t a, c;
			if (b.Peek(a, c) && !(a < 256 && (c & 0xFF) != 0)) {
				if (!LoadFdsSequence(b, Inst.Fds[0], Why) || !LoadFdsSequence(b, Inst.Fds[1], Why))
					return Fail(Why, "instrument " + Number(Slot) + "'s " + Why);
				if (Version > 2 && !LoadFdsSequence(b, Inst.Fds[2], Why))
					return Fail(Why, "instrument " + Number(Slot) + "'s " + Why);
			}
			// Their volumes went to 15 before version 4, then to 31
			if (Version <= 3)
				for (unsigned i = 0; i < Inst.Fds[0].Count; ++i)
					Inst.Fds[0].Items[i] = ToChar(Inst.Fds[0].Items[i] * 2);
			return true;
		}
	}
	return Fail(Why, "an instrument of an unknown kind");
}

bool Document::LoadFdsSequence(BlockReader &b, Sequence &Seq, std::string &Why)
{
	const unsigned Count = b.UChar();
	const unsigned Loop = b.UInt(), Release = b.UInt(), Setting = b.UInt();
	if (Count > unsigned(MAX_SEQUENCE_ITEMS))
		return Fail(Why, "sequence has " + Number(long(Count)) + " items");
	Seq.Clear();
	Seq.SetCount(Count);
	Seq.SetLoop(Loop);
	Seq.SetRelease(Release);
	Seq.Setting = Setting;
	for (unsigned i = 0; i < Count; ++i)
		Seq.SetItem(i, b.Char());
	return true;
}

bool Document::ReadSequences(BlockReader &b, std::string &Why)
{
	const int Version = b.Version();
	const unsigned Count = b.UInt();
	if (Count >= unsigned(MAX_SEQUENCES * SEQ_TYPES))
		return Fail(Why, "it has " + Number(SignedWord(Count)) + " sequences");

	if (Version == 1) {
		// Numbered alone, for any kind: put in order by ReorderSequences
		if (m_TmpSequences.size() < size_t(MAX_SEQUENCES))
			m_TmpSequences.resize(MAX_SEQUENCES);
		for (unsigned i = 0; i < Count && !b.Short(); ++i) {
			const unsigned Index = b.UInt();
			const unsigned SeqCount = b.UChar();
			if (Index >= unsigned(MAX_SEQUENCES))
				return Fail(Why, "it has a sequence numbered " + Number(SignedWord(Index)));
			if (SeqCount >= unsigned(MAX_SEQUENCE_ITEMS))
				return Fail(Why, "sequence " + Number(long(Index)) + " has " + Number(long(SeqCount)) + " items");
			OldSequence &s = m_TmpSequences[Index];
			s.Count = SeqCount;
			for (unsigned j = 0; j < SeqCount; ++j) {
				s.Value[j] = ToChar(b.Char());
				s.Length[j] = ToChar(b.Char());
			}
		}
	}
	else if (Version == 2) {
		if (m_OldSequences.size() != size_t(MAX_SEQUENCES))
			m_OldSequences.resize(MAX_SEQUENCES, std::vector<OldSequence>(SEQ_TYPES));
		for (unsigned i = 0; i < Count && !b.Short(); ++i) {
			const unsigned Index = b.UInt();
			const unsigned Type = b.UInt();
			const unsigned SeqCount = b.UChar();
			if (Index >= unsigned(MAX_SEQUENCES))
				return Fail(Why, "it has a sequence numbered " + Number(SignedWord(Index)));
			if (Type >= unsigned(SEQ_TYPES))
				return Fail(Why, "sequence " + Number(long(Index)) + " is of kind " + Number(SignedWord(Type)));
			if (SeqCount >= unsigned(MAX_SEQUENCE_ITEMS))
				return Fail(Why, "sequence " + Number(long(Index)) + " has " + Number(long(SeqCount)) + " items");
			OldSequence &s = m_OldSequences[Index][Type];
			s.Count = SeqCount;
			for (unsigned j = 0; j < SeqCount; ++j) {
				s.Value[j] = ToChar(b.Char());
				s.Length[j] = ToChar(b.Char());
			}
		}
	}
	else if (Version >= 3) {
		std::vector<unsigned> Indices(Count), Types(Count);
		for (unsigned i = 0; i < Count && !b.Short(); ++i) {
			const unsigned Index = b.UInt();
			const unsigned Type = b.UInt();
			const unsigned SeqCount = b.UChar();
			unsigned LoopPoint = b.UInt();
			// Some older files have the loop point at the end for none
			if (LoopPoint == SeqCount)
				LoopPoint = NO_POINT;
			Indices[i] = Index;
			Types[i] = Type;
			if (Index >= unsigned(MAX_SEQUENCES))
				return Fail(Why, "it has a sequence numbered " + Number(SignedWord(Index)));
			if (Type >= unsigned(SEQ_TYPES))
				return Fail(Why, "sequence " + Number(long(Index)) + " is of kind " + Number(SignedWord(Type)));
			Sequence &s = Seq(MACROS_2A03, int(Index), int(Type));
			s.Clear();
			s.SetCount(SeqCount);
			s.SetLoop(LoopPoint);
			if (Version == 4) {
				const unsigned Release = b.UInt(), Setting = b.UInt();
				s.SetRelease(Release);
				s.Setting = Setting;
			}
			for (unsigned j = 0; j < SeqCount; ++j)
				s.SetItem(j, b.Char());
		}
		if (Version == 5) {
			// Saved in the wrong places: for every slot, applied where there's a sequence
			for (int i = 0; i < MAX_SEQUENCES && !b.Short(); ++i)
				for (int j = 0; j < SEQ_TYPES; ++j) {
					const unsigned Release = b.UInt(), Setting = b.UInt();
					Sequence &s = Seq(MACROS_2A03, i, j);
					if (s.Count > 0) {
						s.SetRelease(Release);
						s.Setting = Setting;
					}
				}
		}
		else if (Version >= 6) {
			for (unsigned i = 0; i < Count && !b.Short(); ++i) {
				const unsigned Release = b.UInt(), Setting = b.UInt();
				Sequence &s = Seq(MACROS_2A03, int(Indices[i]), int(Types[i]));
				s.SetRelease(Release);
				s.Setting = Setting;
			}
		}
	}
	return true;
}

bool Document::ReadSequencesVrc6(BlockReader &b, std::string &Why)
{
	const int Version = b.Version();
	const unsigned Count = b.UInt();
	if (Count >= unsigned(MAX_SEQUENCES * SEQ_TYPES))
		return Fail(Why, "it has " + Number(SignedWord(Count)) + " sequences");

	std::vector<unsigned> Indices(Count), Types(Count);
	for (unsigned i = 0; i < Count && !b.Short(); ++i) {
		const unsigned Index = b.UInt();
		const unsigned Type = b.UInt();
		const unsigned SeqCount = b.UChar();
		const unsigned LoopPoint = b.UInt();
		Indices[i] = Index;
		Types[i] = Type;
		if (Index >= unsigned(MAX_SEQUENCES))
			return Fail(Why, "it has a sequence numbered " + Number(SignedWord(Index)));
		if (Type >= unsigned(SEQ_TYPES))
			return Fail(Why, "sequence " + Number(long(Index)) + " is of kind " + Number(SignedWord(Type)));
		if (SeqCount > unsigned(MAX_SEQUENCE_ITEMS))
			Problem("VRC6 sequence " + Number(long(Index)) + " has " + Number(long(SeqCount)) + " items; FamiTracker keeps 253");
		Sequence &s = Seq(MACROS_VRC6, int(Index), int(Type));
		s.Clear();
		s.SetCount(SeqCount);
		s.SetLoop(LoopPoint);
		if (Version == 4) {
			const unsigned Release = b.UInt(), Setting = b.UInt();
			s.SetRelease(Release);
			s.Setting = Setting;
		}
		for (unsigned j = 0; j < SeqCount; ++j)
			s.SetItem(j, b.Char());
	}
	if (Version == 5) {
		for (int i = 0; i < MAX_SEQUENCES && !b.Short(); ++i)
			for (int j = 0; j < SEQ_TYPES; ++j) {
				const unsigned Release = b.UInt(), Setting = b.UInt();
				Sequence &s = Seq(MACROS_VRC6, i, j);
				if (s.Count > 0) {
					s.SetRelease(Release);
					s.Setting = Setting;
				}
			}
	}
	else if (Version >= 6) {
		for (unsigned i = 0; i < Count && !b.Short(); ++i) {
			const unsigned Release = b.UInt(), Setting = b.UInt();
			Sequence &s = Seq(MACROS_VRC6, int(Indices[i]), int(Types[i]));
			s.SetRelease(Release);
			s.Setting = Setting;
		}
	}
	return true;
}

bool Document::ReadSequencesN163(BlockReader &b, std::string &Why, int Chip)
{
	const unsigned Count = b.UInt();
	if (Count >= unsigned(MAX_SEQUENCES * SEQ_TYPES))
		return Fail(Why, "it has " + Number(SignedWord(Count)) + " sequences");
	for (unsigned i = 0; i < Count && !b.Short(); ++i) {
		const unsigned Index = b.UInt();
		const unsigned Type = b.UInt();
		const unsigned SeqCount = b.UChar();
		const unsigned LoopPoint = b.UInt(), Release = b.UInt(), Setting = b.UInt();
		if (Index >= unsigned(MAX_SEQUENCES))
			return Fail(Why, "it has a sequence numbered " + Number(SignedWord(Index)));
		if (Type >= unsigned(SEQ_TYPES))
			return Fail(Why, "sequence " + Number(long(Index)) + " is of kind " + Number(SignedWord(Type)));
		if (SeqCount > unsigned(MAX_SEQUENCE_ITEMS))
			Problem(std::string(Chip == MACROS_S5B ? "5B" : "N163") + " sequence " + Number(long(Index)) + " has " +
				Number(long(SeqCount)) + " items; FamiTracker keeps 253");
		Sequence &s = Seq(Chip, int(Index), int(Type));
		s.Clear();
		s.SetCount(SeqCount);
		s.SetLoop(LoopPoint);
		s.SetRelease(Release);
		s.Setting = Setting;
		for (unsigned j = 0; j < SeqCount; ++j)
			s.SetItem(j, b.Char());
	}
	return true;
}

bool Document::ReadFrames(BlockReader &b, std::string &Why)
{
	const unsigned Version = unsigned(b.Version());
	if (Version == 1) {
		// One track, and the channels with it
		FtTrack &T = Track(0);
		T.FrameCount = b.UInt();
		m_ChannelsAvailable = b.UInt();
		if (T.FrameCount > unsigned(MAX_FRAMES))
			return Fail(Why, "it has " + Number(SignedWord(T.FrameCount)) + " frames");
		if (m_ChannelsAvailable > MAX_CHANNELS)
			return Fail(Why, "it has " + Number(SignedWord(m_ChannelsAvailable)) + " channels");
		for (unsigned i = 0; i < T.FrameCount; ++i)
			for (unsigned j = 0; j < m_ChannelsAvailable; ++j) {
				const int Pattern = b.Char();
				if (Pattern < 0 || Pattern >= MAX_FRAMES)
					return Fail(Why, "frame " + Number(long(i)) + " plays pattern " + Number(Pattern));
				T.Frames[i][j] = uint8_t(Pattern);
			}
	}
	else if (Version > 1) {
		for (unsigned y = 0; y < m_TrackCount && !b.Short(); ++y) {
			const unsigned FrameCount = b.UInt();
			const unsigned Speed = b.UInt();
			if (FrameCount == 0 || FrameCount > unsigned(MAX_FRAMES))
				return Fail(Why, "track " + Number(long(y) + 1) + " has " + Number(SignedWord(FrameCount)) + " frames");
			if (Speed == 0)
				return Fail(Why, "track " + Number(long(y) + 1) + " has a speed of 0");
			FtTrack &T = Track(y);
			T.FrameCount = FrameCount;
			if (Version == 3) {
				T.Tempo = b.UInt();
				T.Speed = Speed;
			}
			else if (Speed < 20) {
				// Before tempos: a speed, at the machine's tempo
				T.Tempo = m_Machine == NTSC ? DEFAULT_TEMPO_NTSC : DEFAULT_TEMPO_PAL;
				T.Speed = Speed;
			}
			else {
				// or a tempo, at speed 6
				if (Speed > MAX_TEMPO)
					return Fail(Why, "track " + Number(long(y) + 1) + " has a tempo of " + Number(SignedWord(Speed)));
				T.Tempo = Speed;
				T.Speed = DEFAULT_SPEED;
			}
			const unsigned PatternLength = b.UInt();
			if (PatternLength == 0 || PatternLength > unsigned(MAX_PATTERN_LENGTH))
				return Fail(Why, "track " + Number(long(y) + 1) + "'s patterns are " + Number(SignedWord(PatternLength)) + " rows long");
			T.PatternLength = PatternLength;
			for (unsigned i = 0; i < FrameCount; ++i)
				for (unsigned j = 0; j < m_ChannelsAvailable; ++j) {
					const unsigned Pattern = b.UChar();
					if (Pattern >= unsigned(MAX_PATTERN))
						return Fail(Why, "track " + Number(long(y) + 1) + "'s frame " + Number(long(i)) + " plays pattern " + Number(long(Pattern)));
					T.Frames[i][j] = uint8_t(Pattern);
				}
		}
	}
	return true;
}

bool Document::ReadPatterns(BlockReader &b, std::string &Why)
{
	const unsigned Version = unsigned(b.Version());
	if (Version == 0)
		return Fail(Why, "it's version 0, which no FamiTracker writes");	// 0.4.6 would read an unset track number

	if (Version == 1) {
		const int PatternLength = b.Int();
		if (PatternLength <= 0 || PatternLength > MAX_PATTERN_LENGTH)
			return Fail(Why, "its patterns are " + Number(PatternLength) + " rows long");
		Track(0).PatternLength = unsigned(PatternLength);
	}

	bool ManyColumns = false;
	while (!b.Done()) {
		const unsigned TrackIndex = Version > 1 ? b.UInt() : 0;
		const unsigned Channel = b.UInt();
		const unsigned Pattern = b.UInt();
		const unsigned Items = b.UInt();
		if (b.Short())
			return true;
		if (Channel > MAX_CHANNELS) {
			// 0.4.6 reads no further, and the module loads
			Problem("the patterns after one for channel " + Number(SignedWord(Channel)) + " were left out, as FamiTracker 0.4.6 leaves them");
			return true;
		}
		if (TrackIndex >= MAX_TRACKS)
			return Fail(Why, "it has patterns for track " + Number(SignedWord(TrackIndex)));
		if (Channel >= MAX_CHANNELS)
			return Fail(Why, "it has patterns for channel " + Number(long(Channel)));
		if (Pattern >= unsigned(MAX_PATTERN))
			return Fail(Why, "it has a pattern numbered " + Number(SignedWord(Pattern)));
		if (Items - 1 >= unsigned(MAX_PATTERN_LENGTH))
			return Fail(Why, "pattern " + Number(long(Pattern)) + " has " + Number(SignedWord(Items)) + " rows written");

		FtTrack &T = Track(TrackIndex);
		std::map<int, FtNote> &Rows = T.Rows[int(Channel) * MAX_PATTERN + int(Pattern)];
		for (unsigned i = 0; i < Items && !b.Short(); ++i) {
			// 2.00 wrote the row as a byte, and one effect
			const unsigned Row = m_FileVersion == 0x0200 ? unsigned(b.Char()) : b.UInt();
			if (Row >= unsigned(MAX_PATTERN_LENGTH))
				return Fail(Why, "pattern " + Number(long(Pattern)) + " has row " + Number(SignedWord(Row)));

			FtNote n;
			n.Note = uint8_t(b.UChar());
			n.Octave = uint8_t(b.UChar());
			n.Instrument = uint8_t(b.UChar());
			n.Vol = uint8_t(b.UChar());
			const unsigned Columns = m_FileVersion == 0x0200 ? 1 : unsigned(T.Columns[Channel]) + 1;
			for (unsigned c = 0; c < Columns; ++c) {
				unsigned Code = b.UChar(), Param = b.UChar();
				// Before version 3, 3xx was on with its speed less one, and off with an effect of its own
				if (Version < 3) {
					if (Code == EF_PORTAOFF) {
						Code = EF_PORTAMENTO;
						Param = 0;
					}
					else if (Code == EF_PORTAMENTO && Param < 0xFF)
						++Param;
				}
				if (c < unsigned(MAX_EFFECTS)) {
					n.EffNumber[c] = uint8_t(Code);
					n.EffParam[c] = uint8_t(Param);
				}
				else
					ManyColumns = true;
			}

			if (n.Vol > NO_VOLUME)
				n.Vol &= 0x0F;

			if (m_FileVersion == 0x0200) {
				// Speeds were one less, volumes one more (0 for none), and an instrument only came with a note
				if (n.EffNumber[0] == EF_SPEED && n.EffParam[0] < 20)
					++n.EffParam[0];
				if (n.Vol == 0)
					n.Vol = NO_VOLUME;
				else
					n.Vol = uint8_t((n.Vol - 1) & 0x0F);
				if (n.Note == 0)
					n.Instrument = NO_INSTRUMENT;
			}

			if (Version == 3) {
				if ((m_Chip & EXP_VRC7) && Channel > 4) {
					// The VRC7's 1xx and 2xx went the other way
					for (int e = 0; e < MAX_EFFECTS; ++e) {
						if (n.EffNumber[e] == EF_PORTA_DOWN)
							n.EffNumber[e] = EF_PORTA_UP;
						else if (n.EffNumber[e] == EF_PORTA_UP)
							n.EffNumber[e] = EF_PORTA_DOWN;
					}
				}
				else if ((m_Chip & EXP_FDS) && ChannelType(Channel) == CHANID_FDS) {
					// and the FDS's Pxx
					for (int e = 0; e < MAX_EFFECTS; ++e)
						if (n.EffNumber[e] == EF_PITCH && n.EffParam[e] != 0x80)
							n.EffParam[e] = uint8_t((0x100 - n.EffParam[e]) & 0xFF);
				}
			}

			// The FDS played two octaves lower before version 5 (TRANSPOSE_FDS),
			// and its fixed arpeggios move with it
			if (Version < 5 && (m_Chip & EXP_FDS) && ChannelType(Channel) == CHANID_FDS && n.Octave < 6) {
				n.Octave += 2;
				m_AdjustFdsArpeggio = true;
			}

			Rows[int(Row)] = n;
		}
	}
	if (ManyColumns)
		Problem("some cells have more than 4 effect columns; the ones past 4 were left out");
	return true;
}

bool Document::ReadSamples(BlockReader &b, std::string &Why)
{
	const int Count = b.Char();
	if (Count > MAX_DSAMPLES)
		return Fail(Why, "it has " + Number(Count) + " samples");
	for (int i = 0; i < MAX_DSAMPLES; ++i) {
		m_Samples[i].clear();
		m_SampleNames[i].clear();
	}
	for (int i = 0; i < Count && !b.Short(); ++i) {
		const int Index = b.Char();
		if (Index < 0 || Index >= MAX_DSAMPLES)
			return Fail(Why, "it has a sample numbered " + Number(Index));
		const int Length = b.Int();
		if (Length < 0 || Length >= SAMPLE_NAME_MAX)
			return Fail(Why, "sample " + Number(Index) + "'s name is " + Number(Length) + " bytes long");
		m_SampleNames[Index] = b.Text(size_t(Length));
		const int Size = b.Int();
		if (Size < 0 || Size >= SAMPLE_SIZE_MAX)
			return Fail(Why, "sample " + Number(Index) + " is " + Number(Size) + " bytes long");
		m_Samples[Index] = b.Bytes(size_t(Size));
	}
	return true;
}

void Document::ReorderSequences()
{
	// The first versions numbered sequences alone, for any kind: each
	// instrument's are given numbers of their kind, in the order the
	// instruments use them, with volumes kept to 0-15 and duties to 0-3
	int Slots[SEQ_TYPES] = { 0, 0, 0, 0, 0 };
	int Indices[MAX_SEQUENCES][SEQ_TYPES];
	for (int i = 0; i < MAX_SEQUENCES; ++i)
		for (int j = 0; j < SEQ_TYPES; ++j)
			Indices[i][j] = -1;
	m_OldSequences.resize(MAX_SEQUENCES, std::vector<OldSequence>(SEQ_TYPES));

	for (int i = 0; i < MAX_INSTRUMENTS; ++i) {
		FtInstrument &Inst = m_Instruments[i];
		if (Inst.Type != TYPE_2A03)
			continue;
		for (int j = 0; j < SEQ_TYPES; ++j) {
			if (!Inst.SeqEnable[j]) {
				Inst.SeqIndex[j] = 0;
				continue;
			}
			const int Index = Inst.SeqIndex[j];		// 0-127: a byte, negatives left out on loading
			if (Indices[Index][j] >= 0) {
				Inst.SeqIndex[j] = Indices[Index][j];
				continue;
			}
			OldSequence &s = m_OldSequences[Slots[j]][j];
			s = Index < int(m_TmpSequences.size()) ? m_TmpSequences[Index] : OldSequence();
			for (unsigned k = 0; k < s.Count; ++k) {
				if (j == SEQ_VOLUME)
					s.Value[k] = int8_t(std::min(15, std::max(0, int(s.Value[k]))));
				else if (j == SEQ_DUTY)
					s.Value[k] = int8_t(std::min(3, std::max(0, int(s.Value[k]))));
			}
			Indices[Index][j] = Slots[j];
			Inst.SeqIndex[j] = Slots[j];
			++Slots[j];
		}
	}
	m_TmpSequences.clear();
}

void Document::ConvertSequences()
{
	// Old sequences into today's (CFamiTrackerDoc::ConvertSequence): each
	// value for its length, pitches once and then 0, and a negative length
	// making the loop
	for (int i = 0; i < MAX_SEQUENCES; ++i)
		for (int j = 0; j < SEQ_TYPES; ++j) {
			if (i >= int(m_OldSequences.size()))
				continue;
			const OldSequence &Old = m_OldSequences[i][j];
			if (Old.Count == 0 || Old.Count >= unsigned(MAX_SEQUENCE_ITEMS))
				continue;
			Sequence &New = Seq(MACROS_2A03, i, j);
			const int Count = int(Old.Count);
			int LoopPoint = -1, Length = 0, Items = 0;
			for (int k = 0; k < Count; ++k) {
				const int Value = Old.Value[k], Frames = Old.Length[k];
				if (Frames < 0) {
					LoopPoint = 0;
					for (int l = Count + Frames - 1; l < Count - 1; ++l)
						if (l >= 0)
							LoopPoint += Old.Length[l] + 1;
				}
				else {
					for (int l = 0; l < Frames + 1; ++l) {
						if ((j == SEQ_PITCH || j == SEQ_HIPITCH) && l > 0)
							New.SetItem(unsigned(Items++), 0);
						else
							New.SetItem(unsigned(Items++), Value);
						++Length;
					}
				}
			}
			if (LoopPoint != -1) {
				if (LoopPoint > Length)
					LoopPoint = Length;
				LoopPoint = Length - LoopPoint;
			}
			if (Items > MAX_SEQUENCE_ITEMS)
				Problem("an old sequence makes " + Number(Items) + " items; FamiTracker keeps 253");
			New.SetCount(unsigned(Items));
			New.SetLoop(unsigned(LoopPoint));
		}
	m_OldSequences.clear();
}

void Document::Convert()
{
	if (m_FileVersion <= 0x0201)
		ReorderSequences();
	if (m_FileVersion < 0x0300)
		ConvertSequences();

	// The FDS's notes went up two octaves when its patterns were read: fixed arpeggios go with them
	if (m_AdjustFdsArpeggio)
		for (int i = 0; i < MAX_INSTRUMENTS; ++i) {
			Sequence &Arp = m_Instruments[i].Fds[SEQ_ARPEGGIO];
			if (m_Instruments[i].Type == TYPE_FDS && Arp.Count > 0 && Arp.Setting == unsigned(ARP_FIXED))
				for (unsigned j = 0; j < Arp.Count; ++j)
					Arp.Items[j] = ToChar(Arp.Items[j] + 24);
		}
}

Macro MakeMacro(const Sequence &s)
{
	Macro m;
	m.Items.assign(s.Items, s.Items + s.Count);
	m.Loop = SignedWord(s.Loop);
	m.Release = SignedWord(s.Release);
	m.Setting = SignedWord(s.Setting);
	return m;
}

Cell Document::MakeCell(const FtNote &n, bool Noise)
{
	Cell c;
	if (n.Note >= 1 && n.Note <= 12) {
		if (Noise)
			c.Note = (n.Note - 1 + n.Octave * 12) & 0x0F;	// what the noise plays of it, as FamiTracker shows it
		else if (n.Octave < OCTAVES)
			c.Note = n.Octave * 12 + n.Note - 1;
		else
			++m_HighNotes;
	}
	else if (n.Note == FT_RELEASE)
		c.Note = NOTE_RELEASE;
	else if (n.Note == FT_HALT)
		c.Note = NOTE_HALT;
	c.Instrument = n.Instrument == NO_INSTRUMENT ? -1 : n.Instrument;
	c.Volume = n.Vol == NO_VOLUME ? -1 : n.Vol;
	for (int e = 0; e < MAX_EFFECTS; ++e) {
		const int Code = n.EffNumber[e];
		if (Code == 0)
			continue;
		const char Letter = Code <= 32 ? EFF_CHAR[Code - 1] : 0;
		if (Letter)
			c.Effects[e] = Effect(Letter, n.EffParam[e]);
		else
			++m_UnknownEffects;
	}
	return c;
}

bool Document::MakeModule(Module &Out, FtmInfo &Info, std::string &Error)
{
	Module M;
	M.Title = FromWindows(m_Title);
	M.Author = FromWindows(m_Author);
	M.Copyright = FromWindows(m_Copyright);
	// The comment's lines, as File > Export text splits them: at each CR, the LF after it skipped
	if (!m_Comment.empty()) {
		std::string Rest = m_Comment;
		size_t At;
		while ((At = Rest.find('\r')) != std::string::npos) {
			M.Comment.push_back(FromWindows(Rest.substr(0, At)));
			Rest = At + 2 < Rest.size() ? Rest.substr(At + 2) : std::string();
		}
		M.Comment.push_back(FromWindows(Rest));
	}
	M.Machine = int(m_Machine);
	M.FrameRate = SignedWord(m_EngineSpeed);
	M.Expansion = int(m_Chip);
	M.N163Channels = (m_Chip & EXP_N163) ? int(m_NamcoChannels) : 0;
	const unsigned Channels = unsigned(m_ChannelTypes.size());

	// Instruments in their slots, the empty ones marked
	int LastInstrument = -1;
	for (int i = 0; i < MAX_INSTRUMENTS; ++i)
		if (m_Instruments[i].Type != 0)
			LastInstrument = i;
	M.Instruments.resize(size_t(LastInstrument + 1));
	int LastSample = -1;
	int LastMacro[MACRO_CHIPS][SEQ_TYPES];
	for (int Chip = 0; Chip < MACRO_CHIPS; ++Chip)
		for (int t = 0; t < SEQ_TYPES; ++t)
			LastMacro[Chip][t] = -1;
	for (int i = 0; i <= LastInstrument; ++i) {
		const FtInstrument &f = m_Instruments[i];
		Instrument &Inst = M.Instruments[i];
		if (f.Type == 0) {
			Inst.Unused = true;
			continue;
		}
		Inst.Name = FromWindows(f.Name);
		switch (f.Type) {
			case TYPE_2A03:
			case TYPE_VRC6:
			case TYPE_N163:
			case TYPE_S5B: {
				Inst.Kind = f.Type == TYPE_VRC6 ? INST_VRC6 : (f.Type == TYPE_N163 ? INST_N163 : (f.Type == TYPE_S5B ? INST_S5B : INST_2A03));
				const int Chip = f.Type == TYPE_VRC6 ? MACROS_VRC6 : (f.Type == TYPE_N163 ? MACROS_N163 :
					(f.Type == TYPE_S5B ? MACROS_S5B : MACROS_2A03));
				for (int t = 0; t < SEQ_TYPES; ++t) {
					if (!f.SeqEnable[t])
						continue;
					const int Index = f.SeqIndex[t];
					Inst.MacroIndex[t] = Index;
					LastMacro[Chip][t] = std::max(LastMacro[Chip][t], Index);
					const Sequence &s = Seq(Chip, Index, t);
					if (s.Count > 0)
						Inst.Macros[t] = MakeMacro(s);
				}
				if (f.Type == TYPE_2A03) {
					for (int o = 0; o < OCTAVES; ++o)
						for (int n = 0; n < 12; ++n) {
							if (f.Samples[o][n] == 0)
								continue;
							DpcmKey Key;
							Key.Note = o * 12 + n;
							Key.Sample = f.Samples[o][n] - 1;
							Key.Pitch = f.Pitches[o][n] & 0x0F;
							Key.Loop = (f.Pitches[o][n] & 0x80) != 0;
							Key.Delta = f.Deltas[o][n] >= 0 && f.Deltas[o][n] <= 127 ? f.Deltas[o][n] : -1;
							Inst.Keys.push_back(Key);
							LastSample = std::max(LastSample, Key.Sample);
						}
				}
				else if (f.Type == TYPE_N163) {
					Inst.WaveSize = f.WaveSize;
					Inst.WavePos = f.WavePos;
					for (int w = 0; w < f.WaveCount; ++w)
						Inst.Waves.push_back(std::vector<int>(f.Waves[w], f.Waves[w] + f.WaveSize));
				}
				break;
			}
			case TYPE_VRC7:
				Inst.Kind = INST_VRC7;
				Inst.Patch = SignedWord(f.Patch);
				for (int r = 0; r < 8; ++r)
					Inst.Regs[r] = uint8_t(f.Regs[r] & 0xFF);
				break;
			case TYPE_FDS:
				Inst.Kind = INST_FDS;
				Inst.FdsWave.assign(f.Wave, f.Wave + FDS_WAVE_SIZE);
				Inst.FdsMod.assign(f.Mod, f.Mod + FDS_MOD_SIZE);
				Inst.ModRate = f.ModSpeed;
				Inst.ModDepth = f.ModDepth;
				Inst.ModDelay = f.ModDelay;
				for (int t = 0; t < 3; ++t)
					if (f.Fds[t].Count > 0)
						Inst.Macros[t] = MakeMacro(f.Fds[t]);
				break;
		}
	}

	// Macros by their numbers, up to the last with items or that an instrument plays
	for (int Chip = 0; Chip < MACRO_CHIPS; ++Chip)
		for (int t = 0; t < SEQ_TYPES; ++t) {
			int Last = LastMacro[Chip][t];
			for (int i = 0; i < MAX_SEQUENCES; ++i)
				if (Seq(Chip, i, t).Count > 0)
					Last = std::max(Last, i);
			M.Macros[Chip][t].resize(size_t(Last + 1));
			for (int i = 0; i <= Last; ++i)
				if (Seq(Chip, i, t).Count > 0)
					M.Macros[Chip][t][i] = MakeMacro(Seq(Chip, i, t));
		}

	// DPCM samples by their numbers, up to the last there is or that a key plays
	for (int i = 0; i < MAX_DSAMPLES; ++i)
		if (!m_Samples[i].empty())
			LastSample = std::max(LastSample, i);
	M.Samples.resize(size_t(LastSample + 1));
	M.SampleNames.resize(size_t(LastSample + 1));
	for (int i = 0; i <= LastSample; ++i) {
		M.Samples[i] = m_Samples[i];
		M.SampleNames[i] = FromWindows(m_SampleNames[i]);
	}

	// Each track's patterns that a frame plays or that have something in
	// them (the ones File > Export text writes); then made, if there aren't
	// too many
	if (!m_Tracks[0].Allocated)
		Track(0);
	std::vector<std::vector<std::vector<bool> > > Needed(m_TrackCount);
	long Cells = 0;
	for (unsigned t = 0; t < m_TrackCount; ++t) {
		FtTrack &T = m_Tracks[t];
		Needed[t].assign(Channels, std::vector<bool>(MAX_PATTERN, false));
		for (unsigned f = 0; f < T.FrameCount; ++f)
			for (unsigned c = 0; c < Channels; ++c)
				Needed[t][c][T.Frames[f][c]] = true;
		bool Missing = false;
		for (std::map<int, std::map<int, FtNote> >::const_iterator p = T.Rows.begin(); p != T.Rows.end(); ++p) {
			const unsigned c = unsigned(p->first / MAX_PATTERN);
			if (c >= Channels) {
				Missing = true;
				continue;
			}
			for (std::map<int, FtNote>::const_iterator r = p->second.begin(); r != p->second.end(); ++r)
				if (unsigned(r->first) < T.PatternLength && !r->second.Free())
					Needed[t][c][p->first % MAX_PATTERN] = true;
		}
		if (Missing)
			Problem("track " + Number(long(t) + 1) + " has patterns for channels its chips don't have, which FamiTracker leaves out too");
		for (unsigned c = 0; c < Channels; ++c)
			for (int p = 0; p < MAX_PATTERN; ++p)
				if (Needed[t][c][p])
					Cells += long(T.PatternLength);
	}
	for (unsigned t = m_TrackCount; t < MAX_TRACKS; ++t)
		if (!m_Tracks[t].Rows.empty()) {
			Problem("there are patterns for tracks past the module's " + Number(long(m_TrackCount)) + ", which FamiTracker leaves out too");
			break;
		}
	if (Cells > MAX_CELLS) {
		Error = "the module's patterns are too big to read (" + Number(Cells) + " cells)";
		return false;
	}

	for (unsigned t = 0; t < m_TrackCount; ++t) {
		const FtTrack &T = m_Tracks[t];
		nsf::Track Tr;
		Tr.Name = FromWindows(T.Name);
		Tr.PatternLength = int(T.PatternLength);
		Tr.Speed = SignedWord(T.Speed);
		Tr.Tempo = SignedWord(T.Tempo);
		bool ManyColumns = false;
		for (unsigned c = 0; c < Channels; ++c) {
			ManyColumns = ManyColumns || T.Columns[c] >= MAX_EFFECTS;
			Tr.Columns.push_back(std::min(int(T.Columns[c]) + 1, MAX_EFFECTS));
		}
		if (ManyColumns)
			Problem("track " + Number(long(t) + 1) + " has channels with more than 4 effect columns; it keeps 4");
		for (unsigned f = 0; f < T.FrameCount; ++f) {
			std::vector<int> Frame(Channels);
			for (unsigned c = 0; c < Channels; ++c)
				Frame[c] = T.Frames[f][c];
			Tr.Frames.push_back(Frame);
		}
		Tr.Patterns.resize(Channels);
		for (unsigned c = 0; c < Channels; ++c) {
			int Last = -1;
			for (int p = 0; p < MAX_PATTERN; ++p)
				if (Needed[t][c][p])
					Last = p;
			Tr.Patterns[c].resize(size_t(Last + 1));
			for (int p = 0; p <= Last; ++p) {
				if (!Needed[t][c][p])
					continue;
				std::vector<Cell> &Rows = Tr.Patterns[c][p];
				Rows.resize(T.PatternLength);
				std::map<int, std::map<int, FtNote> >::const_iterator Found = T.Rows.find(int(c) * MAX_PATTERN + p);
				if (Found == T.Rows.end())
					continue;
				for (std::map<int, FtNote>::const_iterator r = Found->second.begin(); r != Found->second.end(); ++r)
					if (unsigned(r->first) < T.PatternLength)
						Rows[r->first] = MakeCell(r->second, c == 3);
			}
		}
		M.Tracks.push_back(Tr);
	}
	// Leftovers in the rows of some files, which FamiTracker can't play
	if (m_HighNotes > 0)
		Problem(Number(m_HighNotes) + " note" + (m_HighNotes == 1 ? " is" : "s are") + " in octaves past 7; left out");
	if (m_UnknownEffects > 0)
		Problem(Number(m_UnknownEffects) + " effect" + (m_UnknownEffects == 1 ? " is" : "s are") +
			" of numbers FamiTracker has no letter for (past 32, or its unused 7 and 25), which do nothing; left out");

	Info.FirstHighlight = m_FirstHighlight;
	Info.SecondHighlight = m_SecondHighlight;
	Info.SplitPoint = SignedWord(m_SplitPoint);
	Info.VibratoStyle = m_Vibrato;
	Info.EngineSpeed = SignedWord(m_EngineSpeed);
	M.VibratoStyle = m_Vibrato != 0 ? 1 : 0;
	M.SplitPoint = Info.SplitPoint;
	std::swap(Out, M);
	return true;
}

}	// namespace

bool IsFtm(const std::vector<uint8_t> &Bytes)
{
	return Bytes.size() >= FILE_ID_SIZE && !std::memcmp(Bytes.data(), FILE_ID, FILE_ID_SIZE);
}

bool ParseFtm(const std::vector<uint8_t> &Bytes, Module &Out, FtmInfo &Info, std::string &Error)
{
	Info = FtmInfo();
	if (!IsFtm(Bytes)) {
		Error = "not a FamiTracker module";
		return false;
	}
	if (Bytes.size() < FILE_ID_SIZE + 4) {
		Error = "the module is cut short before its version";
		return false;
	}
	// FamiTracker reads the version's bytes as signed chars: a low byte of
	// 0x80 or more fills the high one (CDocumentFile::ValidateFile)
	const unsigned Low = Bytes[FILE_ID_SIZE], High = Bytes[FILE_ID_SIZE + 1];
	Info.Version = Low | ((Low & 0x80 ? 0xFF : High) << 8);
	if (Info.Version < BLOCKS_VER || Info.Version > FILE_VER) {
		char Version[16];
		std::snprintf(Version, sizeof(Version), "%X.%02X", Info.Version >> 8, Info.Version & 0xFF);
		Error = std::string("the module is of version ") + Version + (Info.Version < BLOCKS_VER ?
			", the first format (FamiTracker 0.1's), which isn't read" : ", newer than FamiTracker 0.4.6 reads (4.40)");
		return false;
	}

	Document Doc(Info.Version, Info.Problems);
	if (!Doc.ReadBlocks(Bytes, Error))
		return false;
	Doc.Convert();
	return Doc.MakeModule(Out, Info, Error);
}

bool ReadFtmFile(const std::string &Path, Module &Out, FtmInfo &Info, std::string &Error)
{
	FILE *f = 0;
#ifdef _MSC_VER
	if (fopen_s(&f, Path.c_str(), "rb") != 0)
		f = 0;
#else
	f = std::fopen(Path.c_str(), "rb");
#endif
	if (!f) {
		Error = "can't open " + Path;
		return false;
	}
	std::vector<uint8_t> Bytes;
	unsigned char Buffer[65536];
	size_t n;
	while ((n = std::fread(Buffer, 1, sizeof(Buffer), f)) > 0) {
		Bytes.insert(Bytes.end(), Buffer, Buffer + n);
		if (Bytes.size() > MAX_FILE) {
			std::fclose(f);
			Error = "the file is too big for a FamiTracker module";
			return false;
		}
	}
	std::fclose(f);
	return ParseFtm(Bytes, Out, Info, Error);
}

TrackReport FtmTrackReport(const Module &M, const FtmInfo &Info, int TrackIndex)
{
	TrackReport R;
	R.Written = true;
	if (TrackIndex < 0 || TrackIndex >= int(M.Tracks.size()))
		return R;
	const Track &T = M.Tracks[size_t(TrackIndex)];
	R.Name = T.Name;
	R.Speed = T.Speed;
	R.Tempo = T.Tempo;
	// FamiTracker's tempo is beats a minute at speed 6 and 4 rows a beat
	const double Rate = M.FrameRate > 0 ? double(M.FrameRate) : (M.Machine == 1 ? 50.0 : 60.0);
	const double RowsPerMinute = T.Speed > 0 && T.Tempo > 0 ? 24.0 * T.Tempo / T.Speed : 0.0;
	R.TicksPerRow = RowsPerMinute > 0 ? Rate * 60.0 / RowsPerMinute : double(DEFAULT_SPEED);
	// A beat as the first highlight marks it, unless that makes the music faster than 200 BPM or slower
	// than 60 (many modules are written at speed 3 with a highlight every 4 rows, a beat in 8), and a
	// bar of three beats when the second highlight says so, else of four (it often marks half bars)
	int Beat = Info.FirstHighlight >= 2 && Info.FirstHighlight <= 16 ? Info.FirstHighlight : 4;
	while (RowsPerMinute > 0 && RowsPerMinute / Beat > 200.0 && Beat < 32)
		Beat *= 2;
	while (RowsPerMinute > 0 && RowsPerMinute / Beat < 60.0 && Beat % 2 == 0 && Beat > 2)
		Beat /= 2;
	R.RowsPerBeat = Beat;
	R.BeatsPerBar = Info.SecondHighlight == 3 * Info.FirstHighlight ? 3 : 4;
	R.FirstBarRow = 0;	// the highlights count from a frame's first row
	R.Bpm = RowsPerMinute / R.RowsPerBeat;
	return R;
}

}	// namespace nsf

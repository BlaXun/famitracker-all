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

#include "Simulate.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace nsf {

namespace {

const double CPU_NTSC = 1789773.0;
const double CPU_PAL = 1662607.0;
const int NOTE_COUNT = 96;
const int VOL_SHIFT = 3;
const int VOL_MAX = 0x7F;

enum seq_state { SEQ_DISABLED, SEQ_RUNNING, SEQ_END, SEQ_HALT };
enum sim_kind { S_PULSE, S_TRIANGLE, S_NOISE, S_DPCM, S_VRC6_PULSE, S_SAW, S_MMC5 };

// Effect letters the importer writes and FamiTracker's meaning of them
enum sim_effect { E_NONE, E_ARP, E_DELAY, E_CUT, E_SWEEP_UP, E_SWEEP_DOWN, E_DUTY, E_PITCH, E_VIBRATO, E_SPEED, E_JUMP, E_SKIP, E_HALT };

int EffectOf(char Letter)
{
	switch (Letter) {
		case '0': return E_ARP;
		case 'G': return E_DELAY;
		case 'S': return E_CUT;
		case 'H': return E_SWEEP_UP;
		case 'I': return E_SWEEP_DOWN;
		case 'V': return E_DUTY;
		case 'P': return E_PITCH;
		case '4': return E_VIBRATO;
		case 'F': return E_SPEED;
		case 'B': return E_JUMP;
		case 'D': return E_SKIP;
		case 'C': return E_HALT;
		default: return E_NONE;
	}
}

struct Tables {
	unsigned Ntsc[NOTE_COUNT], Pal[NOTE_COUNT], Saw[NOTE_COUNT];
	int Vibrato[256];
	Tables()
	{
		const double ClockNtsc = CPU_NTSC / 16.0, ClockPal = CPU_PAL / 16.0;
		for (int i = 0; i < NOTE_COUNT; ++i) {
			const double Freq = 32.7032 * std::pow(2.0, i / 12.0);
			Pal[i] = unsigned(ClockPal / Freq - 0.5);
			Ntsc[i] = unsigned(ClockNtsc / Freq - 0.5);
			Saw[i] = unsigned((ClockNtsc * 16.0) / (Freq * 14.0) - 0.5);
		}
		static const double DEPTH[16] = { 1.0, 1.5, 2.5, 4.0, 5.0, 7.0, 10.0, 12.0, 14.0, 17.0, 22.0, 30.0, 44.0, 64.0, 96.0, 128.0 };
		for (int i = 0; i < 16; ++i)
			for (int j = 0; j < 16; ++j)
				Vibrato[i * 16 + j] = int(std::sin(double(j) / 16.0 * (3.1415 / 2.0)) * DEPTH[i]);
	}
};

const Tables &GetTables()
{
	static const Tables t;
	return t;
}

// The memory the DPCM channel reads: the sample playing, at $C000
class SampleMemory : public SampleReader {
public:
	const std::vector<uint8_t> *Bytes = nullptr;
	uint8_t ReadSample(uint16_t Address) override
	{
		if (!Bytes || Address < 0xC000)
			return 0;
		const size_t i = Address - 0xC000;
		return i < Bytes->size() ? (*Bytes)[i] : 0;
	}
};

struct Sequence {
	const Macro *M = nullptr;
	int Pointer = 0;
	int State = SEQ_DISABLED;
};

struct Channel {
	int Id = 0;
	int Kind = S_PULSE;
	int Column = 0;
	int Instrument = 0;
	bool HadInstrument = false;
	int Note = 0;
	int Period = 0;
	int MaxPeriod = 0x7FF;
	int Volume = VOL_MAX;
	int SeqVolume = 0;
	int InitVolume = 15;
	bool ManualVolume = false;
	int DefaultDuty = 0, Duty = 0;
	bool Gate = false, Release = false;
	int FinePitch = 0x80;
	int Effect = E_NONE;
	int Arpeggio = 0, ArpState = 0;
	int VibratoSpeed = 0, VibratoDepth = 0, VibratoPhase = 0;
	int NoteCut = 0;
	bool DelayEnabled = false;
	int DelayCounter = 0;
	Cell Delayed;
	int DelayColumns = 1;
	// 2A03 pulse sweep
	int Sweep = 0, CSweep = 0;
	bool Sweeping = false;
	int LastPeriod = 0xFFFF;
	Sequence Seqs[SEQ_TYPES];
	// DPCM
	bool DpcmEnabled = false, DpcmTrigger = false;
	int DpcmRate = 0, DpcmLoop = 0, DpcmLength = 0, Dac = 255;
	const std::vector<uint8_t> *Sample = nullptr;
};

class Engine {
public:
	Engine(const Module &M, int TrackIndex) :
		m_M(M), m_T(M.Tracks[TrackIndex]), m_Pal(M.Machine == 1),
		m_Rate(M.FrameRate ? M.FrameRate : (M.Machine == 1 ? 50 : 60)), m_Speed(m_T.Speed), m_Tempo(m_T.Tempo)
	{
		int Col = 0;
		auto Add = [&](int Id, int Kind) {
			Channel c;
			c.Id = Id;
			c.Kind = Kind;
			c.Column = Col++;
			c.MaxPeriod = (Kind == S_VRC6_PULSE || Kind == S_SAW) ? 0xFFF : 0x7FF;
			m_Channels.push_back(c);
		};
		Add(CH_PULSE1, S_PULSE);
		Add(CH_PULSE2, S_PULSE);
		Add(CH_TRIANGLE, S_TRIANGLE);
		Add(CH_NOISE, S_NOISE);
		Add(CH_DPCM, S_DPCM);
		if (M.Expansion & EXP_VRC6) {
			Add(CH_VRC6_PULSE1, S_VRC6_PULSE);
			Add(CH_VRC6_PULSE2, S_VRC6_PULSE);
			Add(CH_VRC6_SAW, S_SAW);
		}
		if (M.Expansion & EXP_MMC5) {
			Add(CH_MMC5_PULSE1, S_MMC5);
			Add(CH_MMC5_PULSE2, S_MMC5);
		}
		SetupSpeed();
	}

	bool Supported() const
	{
		return !(m_M.Expansion & ~(EXP_VRC6 | EXP_MMC5));
	}

	void Run(int Ticks, Recording &Out)
	{
		SampleMemory Memory;
		m_pMemory = &Memory;
		int Flags = 0;
		if (m_M.Expansion & EXP_VRC6) Flags |= CHIP_VRC6;
		if (m_M.Expansion & EXP_MMC5) Flags |= CHIP_MMC5;
		// The recording keeps the samples played, so a comparison knows them by their bytes
		Out = Recording();
		Chips C(Flags, m_Pal, Memory, Out.Sounds);
		m_pChips = &C;
		Out.Pal = m_Pal;
		Out.ChipFlags = Flags;
		Out.TickRate = m_Rate;
		Out.Ticks.assign(CH_COUNT, std::vector<ChannelTick>());
		for (size_t i = 0; i < m_Channels.size(); ++i)
			Out.Channels.push_back(m_Channels[i].Id);

		// As an NSF player starts
		const double TickCycles = (m_Pal ? CPU_PAL : CPU_NTSC) / m_Rate;
		m_Cycle = 0;
		Write(0x4015, 0x0F);
		Write(0x4017, 0x40);
		std::array<ChannelTick, CH_COUNT> Snap;
		for (int Tick = 0; Tick < Ticks && !m_Halted; ++Tick) {
			m_Cycle = uint64_t(Tick * TickCycles) + 10;
			Frame();
			C.Snapshot(uint64_t(Tick * TickCycles + TickCycles / 2), Snap);
			for (size_t i = 0; i < m_Channels.size(); ++i)
				Out.Ticks[m_Channels[i].Id].push_back(Snap[m_Channels[i].Id]);
			Out.Length = Tick + 1;
		}
		for (size_t i = 0; i < m_Channels.size(); ++i)
			Out.Ticks[m_Channels[i].Id].resize(Out.Length);
	}

private:
	void Write(uint16_t Address, uint8_t Value)
	{
		m_pChips->Write(Address, Value, m_Cycle);
		m_Cycle += 4;
	}

	void SetupSpeed()
	{
		m_Decrement = (m_Tempo * 24) / std::max(1, m_Speed);
		m_Remainder = (m_Tempo * 24) % std::max(1, m_Speed);
	}

	const Instrument *GetInstrument(int Index) const
	{
		return Index >= 0 && Index < int(m_M.Instruments.size()) && !m_M.Instruments[Index].Unused ? &m_M.Instruments[Index] : nullptr;
	}

	unsigned TableNote(const Channel &c, int Note) const
	{
		const Tables &T = GetTables();
		Note = std::max(0, std::min(NOTE_COUNT - 1, Note));
		if (c.Kind == S_SAW)
			return T.Saw[Note];
		if (c.Kind == S_VRC6_PULSE || c.Kind == S_MMC5)
			return T.Ntsc[Note];
		return m_Pal ? T.Pal[Note] : T.Ntsc[Note];
	}

	int TriggerNote(const Channel &c, int Note) const
	{
		if (c.Kind == S_NOISE)
			return Note | 0x10;
		return int(TableNote(c, Note));
	}

	void SetPeriod(Channel &c, int Period) { c.Period = std::max(0, std::min(c.MaxPeriod, Period)); }

	// One engine tick, as CSoundGen::OnIdle
	void Frame()
	{
		bool RowRead = false;
		// RunFrame
		if (m_Accum <= 0) {
			RowRead = true;
			ReadRow();
		}
		// PlayChannelNotes
		for (size_t i = 0; i < m_Channels.size(); ++i) {
			Channel &c = m_Channels[i];
			if (m_Queued[i]) {
				m_Queued[i] = false;
				PlayNote(c, m_Cells[i], Columns(c));
			}
		}
		// UpdatePlayer
		if (RowRead && !m_HaltRequest)
			CheckControl();
		if (m_Accum <= 0)
			m_Accum += 60 * m_Rate - m_Remainder;
		m_Accum -= m_Decrement;
		// UpdateChannels
		for (size_t i = 0; i < m_Channels.size(); ++i)
			ProcessChannel(m_Channels[i]);
		// UpdateAPU
		for (size_t i = 0; i < m_Channels.size(); ++i)
			Refresh(m_Channels[i]);
		if (m_HaltRequest)
			m_Halted = true;
	}

	int Columns(const Channel &c) const
	{
		return c.Column < int(m_T.Columns.size()) ? m_T.Columns[c.Column] : 1;
	}

	void ReadRow()
	{
		m_Cells.assign(m_Channels.size(), Cell());
		m_Queued.assign(m_Channels.size(), false);
		if (m_Frame >= int(m_T.Frames.size()))
			return;
		for (size_t i = 0; i < m_Channels.size(); ++i) {
			const int Col = m_Channels[i].Column;
			const int Pattern = Col < int(m_T.Frames[m_Frame].size()) ? m_T.Frames[m_Frame][Col] : 0;
			if (Col < int(m_T.Patterns.size()) && Pattern < int(m_T.Patterns[Col].size()) && m_Row < int(m_T.Patterns[Col][Pattern].size()))
				m_Cells[i] = m_T.Patterns[Col][Pattern][m_Row];
			m_Queued[i] = true;
		}
	}

	void CheckControl()
	{
		if (m_JumpTo >= 0) {
			m_Frame = std::min(m_JumpTo, int(m_T.Frames.size()) - 1);
			m_Row = 0;
		}
		else if (m_SkipTo >= 0) {
			if (++m_Frame >= int(m_T.Frames.size()))
				m_Frame = 0;
			m_Row = std::min(m_SkipTo, m_T.PatternLength - 1);
		}
		else if (++m_Row >= m_T.PatternLength) {
			m_Row = 0;
			if (++m_Frame >= int(m_T.Frames.size()))
				m_Frame = 0;
		}
		m_JumpTo = -1;
		m_SkipTo = -1;
	}

	void PlayNote(Channel &c, const Cell &Data, int Columns)
	{
		// Gxx: the whole row waits
		for (int e = 0; e < Columns; ++e) {
			if (EffectOf(Data.Effects[e].Letter) == E_DELAY && Data.Effects[e].Param > 0) {
				c.DelayEnabled = true;
				c.DelayCounter = Data.Effects[e].Param;
				c.DelayColumns = Columns;
				c.Delayed = Data;
				for (int j = 0; j < Columns; ++j) {
					const int Eff = EffectOf(c.Delayed.Effects[j].Letter);
					if (Eff == E_DELAY)
						c.Delayed.Effects[j] = Effect();
					else if (Eff == E_JUMP) {
						m_JumpTo = c.Delayed.Effects[j].Param;
						c.Delayed.Effects[j] = Effect();
					}
					else if (Eff == E_SKIP) {
						m_SkipTo = c.Delayed.Effects[j].Param;
						c.Delayed.Effects[j] = Effect();
					}
				}
				return;
			}
		}
		// Global effects
		for (int e = 0; e < Columns; ++e) {
			const int Eff = EffectOf(Data.Effects[e].Letter);
			int Param = Data.Effects[e].Param;
			if (Eff == E_SPEED) {
				if (!Param)
					++Param;
				if (Param >= 32)
					m_Tempo = Param;
				else
					m_Speed = Param;
				SetupSpeed();
			}
			else if (Eff == E_JUMP)
				m_JumpTo = Param;
			else if (Eff == E_SKIP)
				m_SkipTo = Param;
			else if (Eff == E_HALT)
				m_HaltRequest = true;
		}
		HandleNoteData(c, Data, Columns);
	}

	void HandleNoteData(Channel &c, const Cell &Data, int Columns)
	{
		// The 2A03's own preparation
		if (c.Kind == S_PULSE || c.Kind == S_TRIANGLE || c.Kind == S_NOISE || c.Kind == S_MMC5) {
			c.Sweep = 0;
			c.Sweeping = false;
			c.InitVolume = 0x0F;
			c.ManualVolume = false;
		}
		const int LastInstrument = c.HadInstrument ? c.Instrument : -1;
		int Inst = Data.Instrument;
		const int Note = Data.Note;
		if (Note != NOTE_NONE)
			c.NoteCut = 0;

		for (int e = 0; e < Columns; ++e)
			Effects(c, EffectOf(Data.Effects[e].Letter), Data.Effects[e].Param);

		if (Data.Volume >= 0 && Data.Volume < 16)
			c.Volume = Data.Volume << VOL_SHIFT;
		if (Note == NOTE_HALT || Note == NOTE_RELEASE)
			Inst = -1;
		if (Inst >= 0) {
			c.Instrument = Inst;
			c.HadInstrument = true;
		}
		const bool Trigger = Note >= 0;
		const bool NewInstrument = !c.HadInstrument || c.Instrument != LastInstrument;
		if (!c.HadInstrument) {
			c.Instrument = 0;
			c.HadInstrument = true;
		}
		if (NewInstrument || Trigger)
			HandleInstrument(c, Trigger);
		if (Note != NOTE_RELEASE && Note != NOTE_NONE)
			c.Release = false;

		if (Note == NOTE_NONE) {
			if (c.ManualVolume)
				c.SeqVolume = c.InitVolume;
			if (c.Sweeping)
				c.CSweep = c.Sweep;
		}
		else if (Note == NOTE_HALT) {
			c.Gate = false;
			c.Period = 0;
			if (c.Kind == S_DPCM)
				c.Gate = false;
		}
		else if (Note == NOTE_RELEASE) {
			if (c.Kind == S_DPCM)
				c.Release = true;
			else if (!c.Release) {
				c.Release = true;
				for (int i = 0; i < SEQ_TYPES; ++i) {
					Sequence &s = c.Seqs[i];
					if ((s.State == SEQ_RUNNING || s.State == SEQ_END) && s.M && s.M->Release >= 0) {
						s.Pointer = s.M->Release;
						s.State = SEQ_RUNNING;
					}
				}
			}
		}
		else
			HandleNote(c, Note);
	}

	void Effects(Channel &c, int Eff, int Param)
	{
		switch (Eff) {
			case E_ARP:
				c.Arpeggio = Param;
				c.Effect = E_ARP;
				break;
			case E_VIBRATO:
				c.VibratoDepth = (Param & 0x0F) << 4;
				c.VibratoSpeed = Param >> 4;
				if (!Param)
					c.VibratoPhase = 0;
				break;
			case E_PITCH:
				c.FinePitch = Param;
				break;
			case E_CUT:
				c.NoteCut = Param + 1;
				break;
			case E_SWEEP_UP:
				if (c.Kind == S_PULSE) {
					c.Sweep = 0x88 | (Param & 0x77);
					c.LastPeriod = 0xFFFF;
					c.Sweeping = true;
				}
				break;
			case E_SWEEP_DOWN:
				if (c.Kind == S_PULSE) {
					c.Sweep = 0x80 | (Param & 0x77);
					c.LastPeriod = 0xFFFF;
					c.Sweeping = true;
				}
				break;
			case E_DUTY:
				c.DefaultDuty = c.Duty = Param;
				break;
		}
	}

	void HandleInstrument(Channel &c, bool Trigger)
	{
		if (c.Kind == S_DPCM)
			return;
		const Instrument *I = GetInstrument(c.Instrument);
		if (!I)
			return;
		for (int i = 0; i < SEQ_TYPES; ++i) {
			const Macro *M = I->Macros[i].Empty() ? nullptr : &I->Macros[i];
			Sequence &s = c.Seqs[i];
			const bool Enabled = M != nullptr;
			if (Trigger || s.M != M || (Enabled && s.State == SEQ_DISABLED)) {
				if (Enabled) {
					s.M = M;
					s.Pointer = 0;
					s.State = SEQ_RUNNING;
				}
				else {
					s.M = nullptr;
					s.Pointer = 0;
					s.State = SEQ_DISABLED;
				}
			}
		}
	}

	void HandleNote(Channel &c, int Note)
	{
		if (c.Kind == S_DPCM) {
			const Instrument *I = GetInstrument(c.Instrument);
			if (!I)
				return;
			for (size_t k = 0; k < I->Keys.size(); ++k) {
				const DpcmKey &Key = I->Keys[k];
				if (Key.Note != Note || Key.Sample < 0 || Key.Sample >= int(m_M.Samples.size()))
					continue;
				const std::vector<uint8_t> &Bytes = m_M.Samples[Key.Sample];
				if (Bytes.empty())
					break;
				c.Sample = &Bytes;
				c.DpcmRate = Key.Pitch & 0x0F;
				c.DpcmLoop = Key.Loop ? 0x40 : 0;
				c.DpcmLength = int(Bytes.size() >> 4);
				c.DpcmEnabled = true;
				c.DpcmTrigger = true;
				c.Gate = true;
				if (Key.Delta >= 0 && c.Dac == 255)
					c.Dac = Key.Delta;
				break;
			}
			return;
		}
		c.Note = Note;
		if (c.Kind == S_NOISE)
			c.Period = (Note & 0x0F) | 0x10;
		else
			c.Period = TriggerNote(c, Note);
		c.Gate = true;
		c.Duty = c.DefaultDuty;
		if (c.Kind == S_VRC6_PULSE || c.Kind == S_SAW)
			c.SeqVolume = 0x0F;
		else
			c.SeqVolume = c.InitVolume;
		if (c.Kind == S_PULSE || c.Kind == S_TRIANGLE) {
			c.ArpState = 0;
			if (!c.Sweeping && (c.CSweep != 0 || c.Sweep != 0)) {
				c.Sweep = 0;
				c.CSweep = 0;
				c.LastPeriod = 0xFFFF;
			}
			else if (c.Sweeping) {
				c.CSweep = c.Sweep;
				c.LastPeriod = 0xFFFF;
			}
		}
	}

	void ProcessChannel(Channel &c)
	{
		// Gxx
		if (c.DelayEnabled) {
			if (!c.DelayCounter) {
				c.DelayEnabled = false;
				PlayNote(c, c.Delayed, c.DelayColumns);
			}
			else
				--c.DelayCounter;
		}
		// Sxx
		if (c.NoteCut > 0 && --c.NoteCut == 0) {
			c.Gate = false;
			c.Period = 0;
		}
		c.VibratoPhase = (c.VibratoPhase + c.VibratoSpeed) & 63;
		// 0xy
		if (c.Effect == E_ARP && c.Arpeggio != 0) {
			switch (c.ArpState) {
				case 0:
					SetPeriod(c, TriggerNote(c, c.Note));
					break;
				case 1:
					SetPeriod(c, TriggerNote(c, c.Note + (c.Arpeggio >> 4)));
					if ((c.Arpeggio & 0x0F) == 0)
						++c.ArpState;
					break;
				case 2:
					SetPeriod(c, TriggerNote(c, c.Note + (c.Arpeggio & 0x0F)));
					break;
			}
			c.ArpState = (c.ArpState + 1) % 3;
		}
		if (c.Kind == S_DPCM)
			return;
		for (int i = 0; i < SEQ_TYPES; ++i)
			RunSequence(c, i);
	}

	void RunSequence(Channel &c, int Type)
	{
		Sequence &s = c.Seqs[Type];
		if (!s.M || s.M->Items.empty() || !c.Gate)
			return;
		if (s.State == SEQ_END) {
			if (Type == SEQ_ARPEGGIO && s.M->Setting == ARP_FIXED)
				SetPeriod(c, TriggerNote(c, c.Note));
			s.State = SEQ_HALT;
			return;
		}
		if (s.State != SEQ_RUNNING)
			return;
		const int Value = s.M->Items[std::min(s.Pointer, int(s.M->Items.size()) - 1)];
		switch (Type) {
			case SEQ_VOLUME:
				c.SeqVolume = Value;
				break;
			case SEQ_ARPEGGIO:
				if (s.M->Setting == ARP_FIXED)
					SetPeriod(c, TriggerNote(c, Value));
				else if (s.M->Setting == ARP_RELATIVE) {
					c.Note += Value;
					SetPeriod(c, TriggerNote(c, c.Note));
				}
				else
					SetPeriod(c, TriggerNote(c, c.Note + Value));
				break;
			case SEQ_PITCH:
				SetPeriod(c, c.Period + Value);
				break;
			case SEQ_HIPITCH:
				SetPeriod(c, c.Period + (Value << 4));
				break;
			case SEQ_DUTY:
				c.Duty = Value;
				break;
		}
		++s.Pointer;
		const int Release = s.M->Release, Items = int(s.M->Items.size()), Loop = s.M->Loop;
		if (s.Pointer == Release + 1 || s.Pointer >= Items) {
			if (Loop != -1 && !(c.Release && Release != -1))
				s.Pointer = Loop;
			else if (s.Pointer >= Items)
				s.State = SEQ_END;
			else if (!c.Release)
				--s.Pointer;
		}
	}

	int Vibrato(const Channel &c) const
	{
		const int *T = GetTables().Vibrato;
		const int Phase = c.VibratoPhase, Depth = c.VibratoDepth;
		if ((Phase & 0xF0) == 0x00) return T[Depth + Phase];
		if ((Phase & 0xF0) == 0x10) return T[Depth + 15 - (Phase - 16)];
		if ((Phase & 0xF0) == 0x20) return -T[Depth + (Phase - 32)];
		return -T[Depth + 15 - (Phase - 48)];
	}

	int CalculatePeriod(const Channel &c) const
	{
		return std::max(0, std::min(c.MaxPeriod, c.Period - Vibrato(c) + (0x80 - c.FinePitch)));
	}

	int CalculateVolume(const Channel &c) const
	{
		int v = (c.SeqVolume * (c.Volume >> VOL_SHIFT)) / 15;
		v = std::max(0, std::min(15, v));
		if (c.SeqVolume > 0 && c.Volume > 0 && v == 0)
			v = 1;
		return c.Gate ? v : 0;
	}

	void Refresh(Channel &c)
	{
		switch (c.Kind) {
			case S_PULSE: {
				const uint16_t Base = c.Id == CH_PULSE1 ? 0x4000 : 0x4004;
				const int Period = CalculatePeriod(c), Volume = CalculateVolume(c);
				const int Hi = Period >> 8, Lo = Period & 0xFF;
				if (!c.Gate || !Volume) {
					Write(Base, 0x30);
					c.LastPeriod = 0xFFFF;
					return;
				}
				Write(Base, uint8_t(((c.Duty & 3) << 6) | 0x30 | Volume));
				if (c.CSweep) {
					if (c.CSweep & 0x80) {
						Write(uint16_t(Base + 1), uint8_t(c.CSweep));
						c.CSweep &= 0x7F;
						Write(0x4017, 0x80);
						Write(0x4017, 0x00);
						Write(uint16_t(Base + 2), uint8_t(Lo));
						Write(uint16_t(Base + 3), uint8_t(Hi));
						c.LastPeriod = 0xFFFF;
					}
				}
				else {
					Write(uint16_t(Base + 1), 0x08);
					Write(0x4017, 0x80);
					Write(0x4017, 0x00);
					Write(uint16_t(Base + 2), uint8_t(Lo));
					if (Hi != (c.LastPeriod >> 8))
						Write(uint16_t(Base + 3), uint8_t(Hi));
				}
				c.LastPeriod = Period;
				break;
			}
			case S_TRIANGLE: {
				const int Period = CalculatePeriod(c);
				if (c.SeqVolume > 0 && c.Volume > 0 && c.Gate) {
					Write(0x4008, 0x81);
					Write(0x400A, uint8_t(Period & 0xFF));
					Write(0x400B, uint8_t(Period >> 8));
				}
				else
					Write(0x4008, 0);
				break;
			}
			case S_NOISE: {
				int Period = CalculatePeriod(c);
				const int Volume = CalculateVolume(c);
				if (!c.Gate || !Volume) {
					Write(0x400C, 0x30);
					return;
				}
				Period = (Period & 0x0F) ^ 0x0F;
				Write(0x400C, uint8_t(0x30 | Volume));
				Write(0x400D, 0);
				Write(0x400E, uint8_t(((c.Duty & 1) << 7) | Period));
				Write(0x400F, 0);
				break;
			}
			case S_DPCM: {
				if (c.Dac != 255) {
					Write(0x4011, uint8_t(c.Dac));
					c.Dac = 255;
				}
				if (c.Release) {
					Write(0x4015, 0x0F);
					c.DpcmEnabled = false;
					c.Release = false;
				}
				if (!c.DpcmEnabled)
					return;
				if (!c.Gate) {
					Write(0x4015, 0x0F);
					Write(0x4011, 0);
					c.DpcmEnabled = false;
				}
				else if (c.DpcmTrigger) {
					m_pMemory->Bytes = c.Sample;
					Write(0x4010, uint8_t(c.DpcmRate | c.DpcmLoop));
					Write(0x4012, 0);
					Write(0x4013, uint8_t(c.DpcmLength));
					Write(0x4015, 0x0F);
					Write(0x4015, 0x1F);
					c.DpcmTrigger = false;
				}
				break;
			}
			case S_VRC6_PULSE: {
				const uint16_t Base = c.Id == CH_VRC6_PULSE1 ? 0x9000 : 0xA000;
				const int Period = CalculatePeriod(c), Volume = CalculateVolume(c);
				if (!c.Gate || !Volume) {
					Write(uint16_t(Base + 2), 0);
					return;
				}
				Write(Base, uint8_t(((c.Duty & 7) << 4) | Volume));
				Write(uint16_t(Base + 1), uint8_t(Period & 0xFF));
				Write(uint16_t(Base + 2), uint8_t(0x80 | (Period >> 8)));
				break;
			}
			case S_SAW: {
				const int Period = CalculatePeriod(c);
				int Volume = (c.SeqVolume * (c.Volume >> VOL_SHIFT)) / 15;
				Volume = (Volume << 1) | ((c.Duty & 1) << 5);
				Volume = std::max(0, std::min(63, Volume));
				if (c.SeqVolume > 0 && c.Volume > 0 && Volume == 0)
					Volume = 2;
				if (!c.Gate || !Volume) {
					Write(0xB002, 0);
					return;
				}
				Write(0xB000, uint8_t(Volume));
				Write(0xB001, uint8_t(Period & 0xFF));
				Write(0xB002, uint8_t(0x80 | (Period >> 8)));
				break;
			}
			case S_MMC5: {
				const uint16_t Base = c.Id == CH_MMC5_PULSE1 ? 0x5000 : 0x5004;
				const int Period = CalculatePeriod(c), Volume = CalculateVolume(c);
				const int Hi = Period >> 8, LastHi = c.LastPeriod >> 8;
				c.LastPeriod = Period;
				Write(0x5015, 0x03);
				Write(Base, uint8_t(((c.Duty & 3) << 6) | 0x30 | Volume));
				Write(uint16_t(Base + 2), uint8_t(Period & 0xFF));
				if (Hi != LastHi)
					Write(uint16_t(Base + 3), uint8_t(Hi));
				break;
			}
		}
	}

	const Module &m_M;
	const Track &m_T;
	bool m_Pal;
	int m_Rate;
	int m_Speed, m_Tempo;
	int m_Decrement = 0, m_Remainder = 0, m_Accum = 0;
	int m_Frame = 0, m_Row = 0;
	int m_JumpTo = -1, m_SkipTo = -1;
	bool m_HaltRequest = false, m_Halted = false;
	std::vector<Channel> m_Channels;
	std::vector<Cell> m_Cells;
	std::vector<bool> m_Queued;
	Chips *m_pChips = nullptr;
	SampleMemory *m_pMemory = nullptr;
	uint64_t m_Cycle = 0;
};

int Rounded(double x) { return int(std::floor(x + 0.5)); }

}	// namespace

bool SimulateTrack(const Module &M, int Track, int Ticks, Recording &Out, std::string &Error)
{
	if (Track < 0 || Track >= int(M.Tracks.size())) {
		Error = "no such track";
		return false;
	}
	Engine E(M, Track);
	if (!E.Supported()) {
		Error = "only the 2A03, VRC6 and MMC5 are simulated";
		return false;
	}
	E.Run(Ticks, Out);
	return true;
}

Agreement Compare(const Recording &A, const Recording &B, int Ticks, int Offset, int Only)
{
	Agreement R;
	long Either = 0, SoundOk = 0, Both = 0, PitchOk = 0;
	double VolumeError = 0;
	for (size_t i = 0; i < A.Channels.size(); ++i) {
		const int c = A.Channels[i];
		if ((Only >= 0 && c != Only) || !B.Has(c) || !A.Has(c))
			continue;
		for (int t = 0; t < Ticks && t < A.Length; ++t) {
			const int tb = t + Offset;
			if (tb < 0 || tb >= B.Length)
				continue;
			const ChannelTick &x = A.Ticks[c][t], &y = B.Ticks[c][tb];
			const bool sa = x.Sounding(), sb = y.Sounding();
			if (sa || sb) {
				++Either;
				if (sa == sb)
					++SoundOk;
			}
			if (sa && sb) {
				++Both;
				// The DPCM's pitch: the sample (by its bytes) and the rate it starts at
				bool Same;
				if (c == CH_DPCM) {
					const bool SameSample = x.Wave < A.Sounds.Samples.size() && y.Wave < B.Sounds.Samples.size() ?
						A.Sounds.Samples[x.Wave] == B.Sounds.Samples[y.Wave] : x.Wave == y.Wave;
					Same = (x.Flags & TICK_TRIGGER) == 0 || (SameSample && x.Timbre == y.Timbre);
				}
				else
					Same = Rounded(x.Pitch) == Rounded(y.Pitch);
				if (Same)
					++PitchOk;
				VolumeError += std::abs(int(x.Volume) - int(y.Volume));
			}
		}
	}
	R.Ticks = int(Either);
	R.Sound = Either ? double(SoundOk) / Either : 1.0;
	R.Pitch = Both ? double(PitchOk) / Both : 1.0;
	R.Volume = Both ? VolumeError / Both : 0.0;
	return R;
}

int BestOffset(const Recording &A, const Recording &B, int Ticks, int Range)
{
	int Best = 0;
	double Score = -1;
	for (int Off = -Range; Off <= Range; ++Off) {
		const Agreement G = Compare(A, B, Ticks, Off);
		const double s = G.Sound + G.Pitch;
		if (s > Score + 1e-9) {
			Score = s;
			Best = Off;
		}
	}
	return Best;
}

}	// namespace nsf

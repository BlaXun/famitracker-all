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

// The NES's CPU (a 6502 without decimal mode) for playing NSF files: every
// official opcode and the stable unofficial ones that music drivers use,
// with their cycle counts. Plain C++14, tested on Linux.

#include <cstdint>

namespace nsf {

// What the CPU reads and writes: memory and the sound chips' registers.
// Cycle is the CPU cycle of the access, counted from the start of the song.
class Bus {
public:
	virtual ~Bus() {}
	virtual uint8_t Read(uint16_t Address, uint64_t Cycle) = 0;
	virtual void Write(uint16_t Address, uint8_t Value, uint64_t Cycle) = 0;
};

class Cpu6502 {
public:
	explicit Cpu6502(Bus &Memory);

	// Calls the subroutine at Address as an NSF player does, with the
	// registers given, until it returns or MaxCycles pass; false if it didn't
	// return in time or ran into an opcode that halts the CPU
	bool Call(uint16_t Address, uint8_t A, uint8_t X, uint8_t Y, uint64_t MaxCycles);

	// One instruction; the cycles it took
	int Step();

	uint64_t Cycles() const { return m_iCycles; }
	void SetCycles(uint64_t Cycles) { m_iCycles = Cycles; }
	bool Jammed() const { return m_bJammed; }

	uint16_t PC;
	uint8_t A, X, Y, S, P;

	// Where a call returns to: an address in the I/O area, which holds no code
	static const uint16_t RETURN_ADDRESS = 0x4100;

private:
	enum {
		FLAG_C = 0x01, FLAG_Z = 0x02, FLAG_I = 0x04, FLAG_D = 0x08,
		FLAG_B = 0x10, FLAG_U = 0x20, FLAG_V = 0x40, FLAG_N = 0x80
	};

	uint8_t Read(uint16_t Address) { return m_Bus.Read(Address, m_iCycles); }
	void Write(uint16_t Address, uint8_t Value) { m_Bus.Write(Address, Value, m_iCycles); }
	uint16_t Read16(uint16_t Address);
	uint16_t Read16ZeroPage(uint8_t Address);
	uint16_t Read16Bug(uint16_t Address);		// JMP ($xxFF) reads the high byte from $xx00
	uint8_t Fetch() { return Read(PC++); }
	uint16_t Fetch16();

	void Push(uint8_t Value) { Write(0x100 | S, Value); --S; }
	uint8_t Pull() { ++S; return Read(0x100 | S); }

	void SetZN(uint8_t Value);
	void SetFlag(uint8_t Flag, bool On);
	bool Flag(uint8_t Flag) const { return (P & Flag) != 0; }

	// Addressing modes: the effective address; Extra is set to 1 when an
	// indexed read crosses a page (one more cycle for reads)
	uint16_t AddrZeroPage() { return Fetch(); }
	uint16_t AddrZeroPageX() { return uint8_t(Fetch() + X); }
	uint16_t AddrZeroPageY() { return uint8_t(Fetch() + Y); }
	uint16_t AddrAbsolute() { return Fetch16(); }
	uint16_t AddrAbsoluteX(int &Extra);
	uint16_t AddrAbsoluteY(int &Extra);
	uint16_t AddrIndirectX();
	uint16_t AddrIndirectY(int &Extra);

	// Operations
	void Adc(uint8_t Value);
	void Sbc(uint8_t Value) { Adc(uint8_t(~Value)); }
	void Compare(uint8_t Register, uint8_t Value);
	uint8_t Asl(uint8_t Value);
	uint8_t Lsr(uint8_t Value);
	uint8_t Rol(uint8_t Value);
	uint8_t Ror(uint8_t Value);
	void Bit(uint8_t Value);
	int Branch(bool Condition);

	Bus &m_Bus;
	uint64_t m_iCycles;
	bool m_bJammed;
};

}	// namespace nsf

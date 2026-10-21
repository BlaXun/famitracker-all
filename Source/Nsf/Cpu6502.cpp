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

#include "Cpu6502.h"

namespace nsf {

namespace {

// Addressing modes
enum mode_t { IMP, ACC, IMM, ZP0, ZPX, ZPY, ABS, ABX, ABY, IND, IZX, IZY, REL };

// Operations, official and unofficial
enum op_t {
	ADC, AND, ASL, BCC, BCS, BEQ, BIT, BMI, BNE, BPL, BRK, BVC, BVS, CLC, CLD, CLI, CLV, CMP, CPX, CPY,
	DEC, DEX, DEY, EOR, INC, INX, INY, JMP, JSR, LDA, LDX, LDY, LSR, NOP, ORA, PHA, PHP, PLA, PLP, ROL,
	ROR, RTI, RTS, SBC, SEC, SED, SEI, STA, STX, STY, TAX, TAY, TSX, TXA, TXS, TYA,
	// unofficial
	KIL, SLO, RLA, SRE, RRA, SAX, LAX, DCP, ISC, ANC, ALR, ARR, XAA, AXS, AHX, TAS, SHY, SHX, LAS
};

struct OpInfo {
	uint8_t Op;
	uint8_t Mode;
	uint8_t Cycles;
	uint8_t PagePenalty;	// one more cycle when an indexed read crosses a page
};

const OpInfo OPS[256] = {
	// 0x00
	{BRK,IMP,7,0}, {ORA,IZX,6,0}, {KIL,IMP,2,0}, {SLO,IZX,8,0}, {NOP,ZP0,3,0}, {ORA,ZP0,3,0}, {ASL,ZP0,5,0}, {SLO,ZP0,5,0},
	{PHP,IMP,3,0}, {ORA,IMM,2,0}, {ASL,ACC,2,0}, {ANC,IMM,2,0}, {NOP,ABS,4,0}, {ORA,ABS,4,0}, {ASL,ABS,6,0}, {SLO,ABS,6,0},
	// 0x10
	{BPL,REL,2,0}, {ORA,IZY,5,1}, {KIL,IMP,2,0}, {SLO,IZY,8,0}, {NOP,ZPX,4,0}, {ORA,ZPX,4,0}, {ASL,ZPX,6,0}, {SLO,ZPX,6,0},
	{CLC,IMP,2,0}, {ORA,ABY,4,1}, {NOP,IMP,2,0}, {SLO,ABY,7,0}, {NOP,ABX,4,1}, {ORA,ABX,4,1}, {ASL,ABX,7,0}, {SLO,ABX,7,0},
	// 0x20
	{JSR,ABS,6,0}, {AND,IZX,6,0}, {KIL,IMP,2,0}, {RLA,IZX,8,0}, {BIT,ZP0,3,0}, {AND,ZP0,3,0}, {ROL,ZP0,5,0}, {RLA,ZP0,5,0},
	{PLP,IMP,4,0}, {AND,IMM,2,0}, {ROL,ACC,2,0}, {ANC,IMM,2,0}, {BIT,ABS,4,0}, {AND,ABS,4,0}, {ROL,ABS,6,0}, {RLA,ABS,6,0},
	// 0x30
	{BMI,REL,2,0}, {AND,IZY,5,1}, {KIL,IMP,2,0}, {RLA,IZY,8,0}, {NOP,ZPX,4,0}, {AND,ZPX,4,0}, {ROL,ZPX,6,0}, {RLA,ZPX,6,0},
	{SEC,IMP,2,0}, {AND,ABY,4,1}, {NOP,IMP,2,0}, {RLA,ABY,7,0}, {NOP,ABX,4,1}, {AND,ABX,4,1}, {ROL,ABX,7,0}, {RLA,ABX,7,0},
	// 0x40
	{RTI,IMP,6,0}, {EOR,IZX,6,0}, {KIL,IMP,2,0}, {SRE,IZX,8,0}, {NOP,ZP0,3,0}, {EOR,ZP0,3,0}, {LSR,ZP0,5,0}, {SRE,ZP0,5,0},
	{PHA,IMP,3,0}, {EOR,IMM,2,0}, {LSR,ACC,2,0}, {ALR,IMM,2,0}, {JMP,ABS,3,0}, {EOR,ABS,4,0}, {LSR,ABS,6,0}, {SRE,ABS,6,0},
	// 0x50
	{BVC,REL,2,0}, {EOR,IZY,5,1}, {KIL,IMP,2,0}, {SRE,IZY,8,0}, {NOP,ZPX,4,0}, {EOR,ZPX,4,0}, {LSR,ZPX,6,0}, {SRE,ZPX,6,0},
	{CLI,IMP,2,0}, {EOR,ABY,4,1}, {NOP,IMP,2,0}, {SRE,ABY,7,0}, {NOP,ABX,4,1}, {EOR,ABX,4,1}, {LSR,ABX,7,0}, {SRE,ABX,7,0},
	// 0x60
	{RTS,IMP,6,0}, {ADC,IZX,6,0}, {KIL,IMP,2,0}, {RRA,IZX,8,0}, {NOP,ZP0,3,0}, {ADC,ZP0,3,0}, {ROR,ZP0,5,0}, {RRA,ZP0,5,0},
	{PLA,IMP,4,0}, {ADC,IMM,2,0}, {ROR,ACC,2,0}, {ARR,IMM,2,0}, {JMP,IND,5,0}, {ADC,ABS,4,0}, {ROR,ABS,6,0}, {RRA,ABS,6,0},
	// 0x70
	{BVS,REL,2,0}, {ADC,IZY,5,1}, {KIL,IMP,2,0}, {RRA,IZY,8,0}, {NOP,ZPX,4,0}, {ADC,ZPX,4,0}, {ROR,ZPX,6,0}, {RRA,ZPX,6,0},
	{SEI,IMP,2,0}, {ADC,ABY,4,1}, {NOP,IMP,2,0}, {RRA,ABY,7,0}, {NOP,ABX,4,1}, {ADC,ABX,4,1}, {ROR,ABX,7,0}, {RRA,ABX,7,0},
	// 0x80
	{NOP,IMM,2,0}, {STA,IZX,6,0}, {NOP,IMM,2,0}, {SAX,IZX,6,0}, {STY,ZP0,3,0}, {STA,ZP0,3,0}, {STX,ZP0,3,0}, {SAX,ZP0,3,0},
	{DEY,IMP,2,0}, {NOP,IMM,2,0}, {TXA,IMP,2,0}, {XAA,IMM,2,0}, {STY,ABS,4,0}, {STA,ABS,4,0}, {STX,ABS,4,0}, {SAX,ABS,4,0},
	// 0x90
	{BCC,REL,2,0}, {STA,IZY,6,0}, {KIL,IMP,2,0}, {AHX,IZY,6,0}, {STY,ZPX,4,0}, {STA,ZPX,4,0}, {STX,ZPY,4,0}, {SAX,ZPY,4,0},
	{TYA,IMP,2,0}, {STA,ABY,5,0}, {TXS,IMP,2,0}, {TAS,ABY,5,0}, {SHY,ABX,5,0}, {STA,ABX,5,0}, {SHX,ABY,5,0}, {AHX,ABY,5,0},
	// 0xA0
	{LDY,IMM,2,0}, {LDA,IZX,6,0}, {LDX,IMM,2,0}, {LAX,IZX,6,0}, {LDY,ZP0,3,0}, {LDA,ZP0,3,0}, {LDX,ZP0,3,0}, {LAX,ZP0,3,0},
	{TAY,IMP,2,0}, {LDA,IMM,2,0}, {TAX,IMP,2,0}, {LAX,IMM,2,0}, {LDY,ABS,4,0}, {LDA,ABS,4,0}, {LDX,ABS,4,0}, {LAX,ABS,4,0},
	// 0xB0
	{BCS,REL,2,0}, {LDA,IZY,5,1}, {KIL,IMP,2,0}, {LAX,IZY,5,1}, {LDY,ZPX,4,0}, {LDA,ZPX,4,0}, {LDX,ZPY,4,0}, {LAX,ZPY,4,0},
	{CLV,IMP,2,0}, {LDA,ABY,4,1}, {TSX,IMP,2,0}, {LAS,ABY,4,1}, {LDY,ABX,4,1}, {LDA,ABX,4,1}, {LDX,ABY,4,1}, {LAX,ABY,4,1},
	// 0xC0
	{CPY,IMM,2,0}, {CMP,IZX,6,0}, {NOP,IMM,2,0}, {DCP,IZX,8,0}, {CPY,ZP0,3,0}, {CMP,ZP0,3,0}, {DEC,ZP0,5,0}, {DCP,ZP0,5,0},
	{INY,IMP,2,0}, {CMP,IMM,2,0}, {DEX,IMP,2,0}, {AXS,IMM,2,0}, {CPY,ABS,4,0}, {CMP,ABS,4,0}, {DEC,ABS,6,0}, {DCP,ABS,6,0},
	// 0xD0
	{BNE,REL,2,0}, {CMP,IZY,5,1}, {KIL,IMP,2,0}, {DCP,IZY,8,0}, {NOP,ZPX,4,0}, {CMP,ZPX,4,0}, {DEC,ZPX,6,0}, {DCP,ZPX,6,0},
	{CLD,IMP,2,0}, {CMP,ABY,4,1}, {NOP,IMP,2,0}, {DCP,ABY,7,0}, {NOP,ABX,4,1}, {CMP,ABX,4,1}, {DEC,ABX,7,0}, {DCP,ABX,7,0},
	// 0xE0
	{CPX,IMM,2,0}, {SBC,IZX,6,0}, {NOP,IMM,2,0}, {ISC,IZX,8,0}, {CPX,ZP0,3,0}, {SBC,ZP0,3,0}, {INC,ZP0,5,0}, {ISC,ZP0,5,0},
	{INX,IMP,2,0}, {SBC,IMM,2,0}, {NOP,IMP,2,0}, {SBC,IMM,2,0}, {CPX,ABS,4,0}, {SBC,ABS,4,0}, {INC,ABS,6,0}, {ISC,ABS,6,0},
	// 0xF0
	{BEQ,REL,2,0}, {SBC,IZY,5,1}, {KIL,IMP,2,0}, {ISC,IZY,8,0}, {NOP,ZPX,4,0}, {SBC,ZPX,4,0}, {INC,ZPX,6,0}, {ISC,ZPX,6,0},
	{SED,IMP,2,0}, {SBC,ABY,4,1}, {NOP,IMP,2,0}, {ISC,ABY,7,0}, {NOP,ABX,4,1}, {SBC,ABX,4,1}, {INC,ABX,7,0}, {ISC,ABX,7,0},
};

}	// namespace

Cpu6502::Cpu6502(Bus &Memory) :
	PC(0), A(0), X(0), Y(0), S(0xFD), P(FLAG_U | FLAG_I), m_Bus(Memory), m_iCycles(0), m_bJammed(false)
{
}

uint16_t Cpu6502::Read16(uint16_t Address)
{
	const uint8_t Lo = Read(Address);
	return uint16_t(Lo | (Read(uint16_t(Address + 1)) << 8));
}

uint16_t Cpu6502::Read16ZeroPage(uint8_t Address)
{
	const uint8_t Lo = Read(Address);
	return uint16_t(Lo | (Read(uint8_t(Address + 1)) << 8));
}

uint16_t Cpu6502::Read16Bug(uint16_t Address)
{
	const uint8_t Lo = Read(Address);
	const uint16_t Next = uint16_t((Address & 0xFF00) | uint8_t(Address + 1));
	return uint16_t(Lo | (Read(Next) << 8));
}

uint16_t Cpu6502::Fetch16()
{
	const uint8_t Lo = Fetch();
	return uint16_t(Lo | (Fetch() << 8));
}

void Cpu6502::SetZN(uint8_t Value)
{
	SetFlag(FLAG_Z, Value == 0);
	SetFlag(FLAG_N, (Value & 0x80) != 0);
}

void Cpu6502::SetFlag(uint8_t Which, bool On)
{
	if (On)
		P |= Which;
	else
		P &= uint8_t(~Which);
}

uint16_t Cpu6502::AddrAbsoluteX(int &Extra)
{
	const uint16_t Base = Fetch16();
	const uint16_t Addr = uint16_t(Base + X);
	Extra = (Base & 0xFF00) != (Addr & 0xFF00) ? 1 : 0;
	return Addr;
}

uint16_t Cpu6502::AddrAbsoluteY(int &Extra)
{
	const uint16_t Base = Fetch16();
	const uint16_t Addr = uint16_t(Base + Y);
	Extra = (Base & 0xFF00) != (Addr & 0xFF00) ? 1 : 0;
	return Addr;
}

uint16_t Cpu6502::AddrIndirectX()
{
	return Read16ZeroPage(uint8_t(Fetch() + X));
}

uint16_t Cpu6502::AddrIndirectY(int &Extra)
{
	const uint16_t Base = Read16ZeroPage(Fetch());
	const uint16_t Addr = uint16_t(Base + Y);
	Extra = (Base & 0xFF00) != (Addr & 0xFF00) ? 1 : 0;
	return Addr;
}

void Cpu6502::Adc(uint8_t Value)
{
	// The 2A03 has no decimal mode
	const unsigned Sum = unsigned(A) + Value + (Flag(FLAG_C) ? 1 : 0);
	SetFlag(FLAG_C, Sum > 0xFF);
	SetFlag(FLAG_V, (~(A ^ Value) & (A ^ Sum) & 0x80) != 0);
	A = uint8_t(Sum);
	SetZN(A);
}

void Cpu6502::Compare(uint8_t Register, uint8_t Value)
{
	SetFlag(FLAG_C, Register >= Value);
	SetZN(uint8_t(Register - Value));
}

uint8_t Cpu6502::Asl(uint8_t Value)
{
	SetFlag(FLAG_C, (Value & 0x80) != 0);
	Value = uint8_t(Value << 1);
	SetZN(Value);
	return Value;
}

uint8_t Cpu6502::Lsr(uint8_t Value)
{
	SetFlag(FLAG_C, (Value & 0x01) != 0);
	Value = uint8_t(Value >> 1);
	SetZN(Value);
	return Value;
}

uint8_t Cpu6502::Rol(uint8_t Value)
{
	const bool Carry = Flag(FLAG_C);
	SetFlag(FLAG_C, (Value & 0x80) != 0);
	Value = uint8_t((Value << 1) | (Carry ? 1 : 0));
	SetZN(Value);
	return Value;
}

uint8_t Cpu6502::Ror(uint8_t Value)
{
	const bool Carry = Flag(FLAG_C);
	SetFlag(FLAG_C, (Value & 0x01) != 0);
	Value = uint8_t((Value >> 1) | (Carry ? 0x80 : 0));
	SetZN(Value);
	return Value;
}

void Cpu6502::Bit(uint8_t Value)
{
	SetFlag(FLAG_Z, (A & Value) == 0);
	SetFlag(FLAG_V, (Value & 0x40) != 0);
	SetFlag(FLAG_N, (Value & 0x80) != 0);
}

int Cpu6502::Branch(bool Condition)
{
	const int8_t Offset = int8_t(Fetch());
	if (!Condition)
		return 0;
	const uint16_t Target = uint16_t(PC + Offset);
	const int Extra = (Target & 0xFF00) != (PC & 0xFF00) ? 2 : 1;
	PC = Target;
	return Extra;
}

int Cpu6502::Step()
{
	if (m_bJammed)
		return 0;

	const uint8_t Opcode = Fetch();
	const OpInfo &Info = OPS[Opcode];
	int Cycles = Info.Cycles;
	int Extra = 0;
	uint16_t Addr = 0;

	switch (Info.Mode) {
		case IMP: case ACC: break;
		case IMM: Addr = PC++; break;
		case ZP0: Addr = AddrZeroPage(); break;
		case ZPX: Addr = AddrZeroPageX(); break;
		case ZPY: Addr = AddrZeroPageY(); break;
		case ABS: Addr = AddrAbsolute(); break;
		case ABX: Addr = AddrAbsoluteX(Extra); break;
		case ABY: Addr = AddrAbsoluteY(Extra); break;
		case IND: Addr = Read16Bug(Fetch16()); break;
		case IZX: Addr = AddrIndirectX(); break;
		case IZY: Addr = AddrIndirectY(Extra); break;
		case REL: break;
	}
	if (Info.PagePenalty)
		Cycles += Extra;

	// Advance the clock to the end of the instruction before its memory
	// access, so that register writes are stamped close to when they happen
	m_iCycles += Cycles - 1;

	uint8_t V;
	switch (Info.Op) {
		// Loads and stores
		case LDA: A = Read(Addr); SetZN(A); break;
		case LDX: X = Read(Addr); SetZN(X); break;
		case LDY: Y = Read(Addr); SetZN(Y); break;
		case STA: Write(Addr, A); break;
		case STX: Write(Addr, X); break;
		case STY: Write(Addr, Y); break;

		// Arithmetic and logic
		case ADC: Adc(Read(Addr)); break;
		case SBC: Sbc(Read(Addr)); break;
		case AND: A &= Read(Addr); SetZN(A); break;
		case ORA: A |= Read(Addr); SetZN(A); break;
		case EOR: A ^= Read(Addr); SetZN(A); break;
		case CMP: Compare(A, Read(Addr)); break;
		case CPX: Compare(X, Read(Addr)); break;
		case CPY: Compare(Y, Read(Addr)); break;
		case BIT: Bit(Read(Addr)); break;

		// Shifts and increments
		case ASL:
			if (Info.Mode == ACC) A = Asl(A);
			else Write(Addr, Asl(Read(Addr)));
			break;
		case LSR:
			if (Info.Mode == ACC) A = Lsr(A);
			else Write(Addr, Lsr(Read(Addr)));
			break;
		case ROL:
			if (Info.Mode == ACC) A = Rol(A);
			else Write(Addr, Rol(Read(Addr)));
			break;
		case ROR:
			if (Info.Mode == ACC) A = Ror(A);
			else Write(Addr, Ror(Read(Addr)));
			break;
		case INC: V = uint8_t(Read(Addr) + 1); SetZN(V); Write(Addr, V); break;
		case DEC: V = uint8_t(Read(Addr) - 1); SetZN(V); Write(Addr, V); break;
		case INX: ++X; SetZN(X); break;
		case INY: ++Y; SetZN(Y); break;
		case DEX: --X; SetZN(X); break;
		case DEY: --Y; SetZN(Y); break;

		// Transfers
		case TAX: X = A; SetZN(X); break;
		case TAY: Y = A; SetZN(Y); break;
		case TXA: A = X; SetZN(A); break;
		case TYA: A = Y; SetZN(A); break;
		case TSX: X = S; SetZN(X); break;
		case TXS: S = X; break;

		// Stack
		case PHA: Push(A); break;
		case PHP: Push(uint8_t(P | FLAG_B | FLAG_U)); break;
		case PLA: A = Pull(); SetZN(A); break;
		case PLP: P = uint8_t((Pull() & ~FLAG_B) | FLAG_U); break;

		// Flags
		case CLC: SetFlag(FLAG_C, false); break;
		case SEC: SetFlag(FLAG_C, true); break;
		case CLI: SetFlag(FLAG_I, false); break;
		case SEI: SetFlag(FLAG_I, true); break;
		case CLV: SetFlag(FLAG_V, false); break;
		case CLD: SetFlag(FLAG_D, false); break;
		case SED: SetFlag(FLAG_D, true); break;

		// Branches
		case BCC: Cycles += Branch(!Flag(FLAG_C)); break;
		case BCS: Cycles += Branch(Flag(FLAG_C)); break;
		case BEQ: Cycles += Branch(Flag(FLAG_Z)); break;
		case BNE: Cycles += Branch(!Flag(FLAG_Z)); break;
		case BMI: Cycles += Branch(Flag(FLAG_N)); break;
		case BPL: Cycles += Branch(!Flag(FLAG_N)); break;
		case BVC: Cycles += Branch(!Flag(FLAG_V)); break;
		case BVS: Cycles += Branch(Flag(FLAG_V)); break;

		// Jumps
		case JMP: PC = Addr; break;
		case JSR: {
			const uint16_t Return = uint16_t(PC - 1);
			Push(uint8_t(Return >> 8));
			Push(uint8_t(Return));
			PC = Addr;
			break;
		}
		case RTS: {
			const uint8_t Lo = Pull();
			PC = uint16_t(((Pull() << 8) | Lo) + 1);
			break;
		}
		case RTI: {
			P = uint8_t((Pull() & ~FLAG_B) | FLAG_U);
			const uint8_t Lo = Pull();
			PC = uint16_t((Pull() << 8) | Lo);
			break;
		}
		case BRK: {
			const uint16_t Return = uint16_t(PC + 1);
			Push(uint8_t(Return >> 8));
			Push(uint8_t(Return));
			Push(uint8_t(P | FLAG_B | FLAG_U));
			SetFlag(FLAG_I, true);
			PC = Read16(0xFFFE);
			break;
		}

		case NOP:
			if (Info.Mode != IMP)
				Read(Addr);		// the unofficial NOPs read their operand
			break;

		// Unofficial opcodes that music drivers have been seen to use
		case KIL: m_bJammed = true; --PC; break;
		case SLO: V = Asl(Read(Addr)); Write(Addr, V); A |= V; SetZN(A); break;
		case RLA: V = Rol(Read(Addr)); Write(Addr, V); A &= V; SetZN(A); break;
		case SRE: V = Lsr(Read(Addr)); Write(Addr, V); A ^= V; SetZN(A); break;
		case RRA: V = Ror(Read(Addr)); Write(Addr, V); Adc(V); break;
		case SAX: Write(Addr, uint8_t(A & X)); break;
		case LAX: A = X = Read(Addr); SetZN(A); break;
		case DCP: V = uint8_t(Read(Addr) - 1); Write(Addr, V); Compare(A, V); break;
		case ISC: V = uint8_t(Read(Addr) + 1); Write(Addr, V); Sbc(V); break;
		case ANC: A &= Read(Addr); SetZN(A); SetFlag(FLAG_C, (A & 0x80) != 0); break;
		case ALR: A &= Read(Addr); A = Lsr(A); break;
		case ARR: {
			A &= Read(Addr);
			A = uint8_t((A >> 1) | (Flag(FLAG_C) ? 0x80 : 0));
			SetZN(A);
			SetFlag(FLAG_C, (A & 0x40) != 0);
			SetFlag(FLAG_V, (((A >> 6) ^ (A >> 5)) & 1) != 0);
			break;
		}
		case XAA: A = uint8_t(X & Read(Addr)); SetZN(A); break;
		case AXS: {
			const uint8_t Operand = Read(Addr);
			const uint8_t AX = uint8_t(A & X);
			SetFlag(FLAG_C, AX >= Operand);
			X = uint8_t(AX - Operand);
			SetZN(X);
			break;
		}
		case AHX: Write(Addr, uint8_t(A & X & ((Addr >> 8) + 1))); break;
		case TAS: S = uint8_t(A & X); Write(Addr, uint8_t(S & ((Addr >> 8) + 1))); break;
		case SHY: Write(Addr, uint8_t(Y & ((Addr >> 8) + 1))); break;
		case SHX: Write(Addr, uint8_t(X & ((Addr >> 8) + 1))); break;
		case LAS: A = X = S = uint8_t(Read(Addr) & S); SetZN(A); break;
	}

	// Branches taken add cycles after the operation
	m_iCycles += Cycles - (Info.Cycles + (Info.PagePenalty ? Extra : 0)) + 1;
	return Cycles;
}

bool Cpu6502::Call(uint16_t Address, uint8_t RegA, uint8_t RegX, uint8_t RegY, uint64_t MaxCycles)
{
	A = RegA;
	X = RegX;
	Y = RegY;
	P = FLAG_U | FLAG_I;
	S = 0xFD;
	const uint16_t Return = uint16_t(RETURN_ADDRESS - 1);
	Push(uint8_t(Return >> 8));
	Push(uint8_t(Return));
	PC = Address;
	m_bJammed = false;

	const uint64_t Limit = m_iCycles + MaxCycles;
	while (PC != RETURN_ADDRESS) {
		if (m_bJammed || m_iCycles >= Limit)
			return false;
		Step();
	}
	return true;
}

}	// namespace nsf

#pragma once
//
// Maki bytecode opcodes — port of Src/Wasabi/api/script/opcodes.h.
// Numerical values match the upstream binary format and must NOT be
// renumbered.

namespace wasabiq::maki {

constexpr unsigned OP_NOP    = 0x00;

constexpr unsigned OP_PUSH   = 0x01;
constexpr unsigned OP_POPI   = 0x02;
constexpr unsigned OP_POP    = 0x03;

constexpr unsigned OP_CMPEQ  = 0x08;
constexpr unsigned OP_CMPNE  = 0x09;
constexpr unsigned OP_CMPA   = 0x0A;
constexpr unsigned OP_CMPAE  = 0x0B;
constexpr unsigned OP_CMPB   = 0x0C;
constexpr unsigned OP_CMPBE  = 0x0D;

constexpr unsigned OP_JIZ    = 0x10;
constexpr unsigned OP_JNZ    = 0x11;
constexpr unsigned OP_JMP    = 0x12;

constexpr unsigned OP_CALLM  = 0x18;
constexpr unsigned OP_CALLC  = 0x19;

constexpr unsigned OP_RET    = 0x20;
constexpr unsigned OP_RETF   = 0x21;

constexpr unsigned OP_CMPLT  = 0x28;

constexpr unsigned OP_SET    = 0x30;

constexpr unsigned OP_INCS   = 0x38;
constexpr unsigned OP_DECS   = 0x39;
constexpr unsigned OP_INCP   = 0x3A;
constexpr unsigned OP_DECP   = 0x3B;

constexpr unsigned OP_ADD    = 0x40;
constexpr unsigned OP_SUB    = 0x41;
constexpr unsigned OP_MUL    = 0x42;
constexpr unsigned OP_DIV    = 0x43;
constexpr unsigned OP_MOD    = 0x44;

constexpr unsigned OP_AND    = 0x48;
constexpr unsigned OP_OR     = 0x49;
constexpr unsigned OP_NOT    = 0x4A;
constexpr unsigned OP_BNOT   = 0x4B;
constexpr unsigned OP_NEG    = 0x4C;
constexpr unsigned OP_XOR    = 0x4D;

constexpr unsigned OP_LAND   = 0x50;
constexpr unsigned OP_LOR    = 0x51;

constexpr unsigned OP_SHL    = 0x58;
constexpr unsigned OP_SHR    = 0x59;

constexpr unsigned OP_NEW    = 0x60;
constexpr unsigned OP_DELETE = 0x61;

constexpr unsigned OP_UMV    = 0x68;
constexpr unsigned OP_UMC    = 0x69;

constexpr unsigned OP_CALLM2 = 0x70;

} // namespace wasabiq::maki

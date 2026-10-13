#pragma once

#include <cstdint>

namespace trek::dos {

enum Reg16 : int { AX, CX, DX, BX, SP, BP, SI, DI };
enum SegReg : int { ES, CS, SS, DS };

// Real-mode 8086 register file. Flags are kept unpacked; pack()/unpack()
// convert to the FLAGS word for PUSHF/POPF/interrupts.
struct Cpu {
    std::uint16_t r[8] = {};
    std::uint16_t s[4] = {};
    bool cf = false, pf = false, af = false, zf = false, sf = false, of = false;
    bool df = false, ifl = true, tf = false;
    std::uint32_t pc = 0;  // linear address of the next instruction or block

    std::uint8_t get8(int n) const { return n < 4 ? r[n] & 0xFF : r[n - 4] >> 8; }
    void set8(int n, std::uint8_t v) {
        if (n < 4)
            r[n] = static_cast<std::uint16_t>((r[n] & 0xFF00) | v);
        else
            r[n - 4] = static_cast<std::uint16_t>((r[n - 4] & 0x00FF) | (v << 8));
    }
    std::uint16_t ip() const { return static_cast<std::uint16_t>(pc - s[CS] * 16u); }

    // 8086 FLAGS: bits 12-15 always read as 1, bit 1 always 1.
    std::uint16_t pack() const {
        return static_cast<std::uint16_t>(0xF002 | cf | pf << 2 | af << 4 | zf << 6 | sf << 7 | tf << 8 |
                                          ifl << 9 | df << 10 | of << 11);
    }
    void unpack(std::uint16_t f) {
        cf = f & 1, pf = f & 4, af = f & 0x10, zf = f & 0x40, sf = f & 0x80;
        tf = f & 0x100, ifl = f & 0x200, df = f & 0x400, of = f & 0x800;
    }
};

}  // namespace trek::dos

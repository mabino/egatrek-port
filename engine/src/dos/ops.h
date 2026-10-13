#pragma once

// Instruction semantics shared by recompiled code and the interpreter.
// Every function computes the result and the flags the 8086 defines for it.
// Where Intel leaves a flag undefined we pick one fixed behaviour; tests do
// not compare undefined flags.

#include <cstdint>

#include "dos/cpu.h"

namespace trek::dos::op {

template <int W>
struct Bits;
template <>
struct Bits<8> {
    using T = std::uint8_t;
    using S = std::int8_t;
    static constexpr unsigned kMask = 0xFF, kSign = 0x80;
};
template <>
struct Bits<16> {
    using T = std::uint16_t;
    using S = std::int16_t;
    static constexpr unsigned kMask = 0xFFFF, kSign = 0x8000;
};

inline bool parity(unsigned v) {
    v &= 0xFF;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return !(v & 1);
}

template <int W>
inline void szp(Cpu& c, unsigned r) {
    r &= Bits<W>::kMask;
    c.zf = r == 0;
    c.sf = r & Bits<W>::kSign;
    c.pf = parity(r);
}

template <int W>
inline typename Bits<W>::T add(Cpu& c, unsigned a, unsigned b, unsigned carry = 0) {
    const unsigned r = a + b + carry;
    c.cf = r > Bits<W>::kMask;
    c.af = (a ^ b ^ r) & 0x10;
    c.of = ((a ^ r) & (b ^ r) & Bits<W>::kSign) != 0;
    szp<W>(c, r);
    return static_cast<typename Bits<W>::T>(r);
}

template <int W>
inline typename Bits<W>::T sub(Cpu& c, unsigned a, unsigned b, unsigned borrow = 0) {
    const unsigned r = a - b - borrow;
    c.cf = b + borrow > a;
    c.af = (a ^ b ^ r) & 0x10;
    c.of = ((a ^ b) & (a ^ r) & Bits<W>::kSign) != 0;
    szp<W>(c, r);
    return static_cast<typename Bits<W>::T>(r);
}

template <int W>
inline typename Bits<W>::T adc(Cpu& c, unsigned a, unsigned b) { return add<W>(c, a, b, c.cf); }
template <int W>
inline typename Bits<W>::T sbb(Cpu& c, unsigned a, unsigned b) { return sub<W>(c, a, b, c.cf); }

template <int W>
inline typename Bits<W>::T logic(Cpu& c, unsigned r) {
    c.cf = c.of = c.af = false;
    szp<W>(c, r);
    return static_cast<typename Bits<W>::T>(r);
}
template <int W>
inline typename Bits<W>::T and_(Cpu& c, unsigned a, unsigned b) { return logic<W>(c, a & b); }
template <int W>
inline typename Bits<W>::T or_(Cpu& c, unsigned a, unsigned b) { return logic<W>(c, a | b); }
template <int W>
inline typename Bits<W>::T xor_(Cpu& c, unsigned a, unsigned b) { return logic<W>(c, a ^ b); }

template <int W>
inline typename Bits<W>::T inc(Cpu& c, unsigned a) {
    const bool cf = c.cf;
    auto r = add<W>(c, a, 1);
    c.cf = cf;
    return r;
}
template <int W>
inline typename Bits<W>::T dec(Cpu& c, unsigned a) {
    const bool cf = c.cf;
    auto r = sub<W>(c, a, 1);
    c.cf = cf;
    return r;
}
template <int W>
inline typename Bits<W>::T neg(Cpu& c, unsigned a) { return sub<W>(c, 0, a); }

// Shifts and rotates. Counts are masked to 5 bits (80186 and later; the
// 8086 did not mask, but no count above 31 is meaningful for this game).
// A zero count changes nothing, flags included.
template <int W>
inline typename Bits<W>::T shift(Cpu& c, int kind, unsigned a, unsigned count) {
    using T = typename Bits<W>::T;
    constexpr unsigned M = Bits<W>::kMask, S = Bits<W>::kSign;
    count &= 0x1F;
    if (count == 0) return static_cast<T>(a);
    a &= M;
    unsigned r = a;
    switch (kind) {
        case 0:  // rol
            for (unsigned i = 0; i < count; ++i) r = ((r << 1) | (r & S ? 1 : 0)) & M;
            c.cf = r & 1;
            c.of = ((r & S) != 0) != c.cf;
            return static_cast<T>(r);
        case 1:  // ror
            for (unsigned i = 0; i < count; ++i) r = (r >> 1) | (r & 1 ? S : 0);
            c.cf = r & S;
            c.of = ((r ^ (r << 1)) & S) != 0;
            return static_cast<T>(r);
        case 2:  // rcl
            for (unsigned i = 0; i < count; ++i) {
                const bool out = r & S;
                r = ((r << 1) | c.cf) & M;
                c.cf = out;
            }
            c.of = ((r & S) != 0) != c.cf;
            return static_cast<T>(r);
        case 3:  // rcr
            for (unsigned i = 0; i < count; ++i) {
                const bool out = r & 1;
                r = (r >> 1) | (c.cf ? S : 0);
                c.cf = out;
            }
            c.of = ((r ^ (r << 1)) & S) != 0;
            return static_cast<T>(r);
        case 4:  // shl / sal
        case 6:
            c.cf = count <= W ? (a >> (W - count)) & 1 : 0;
            r = count < 32 ? (a << count) & M : 0;
            c.of = ((r & S) != 0) != c.cf;
            break;
        case 5:  // shr
            c.cf = count <= W ? (a >> (count - 1)) & 1 : 0;
            r = count < W ? a >> count : 0;
            c.of = a & S;
            break;
        default: {  // sar
            const int sa = static_cast<typename Bits<W>::S>(a);
            const unsigned n = count < W ? count : W - 1;
            c.cf = count < W ? (sa >> (count - 1)) & 1 : (a & S) != 0;
            r = static_cast<unsigned>(sa >> n) & M;
            c.of = false;
            break;
        }
    }
    c.af = false;
    szp<W>(c, r);
    return static_cast<T>(r);
}

// MUL/IMUL leave SF/ZF/PF undefined; we leave them unchanged.
inline void mul8(Cpu& c, std::uint8_t b) {
    c.r[AX] = static_cast<std::uint16_t>((c.r[AX] & 0xFF) * b);
    c.cf = c.of = (c.r[AX] >> 8) != 0;
}
inline void mul16(Cpu& c, std::uint16_t b) {
    const std::uint32_t r = static_cast<std::uint32_t>(c.r[AX]) * b;
    c.r[AX] = static_cast<std::uint16_t>(r);
    c.r[DX] = static_cast<std::uint16_t>(r >> 16);
    c.cf = c.of = c.r[DX] != 0;
}
inline void imul8(Cpu& c, std::uint8_t b) {
    const int r = static_cast<std::int8_t>(c.r[AX] & 0xFF) * static_cast<std::int8_t>(b);
    c.r[AX] = static_cast<std::uint16_t>(r);
    c.cf = c.of = r != static_cast<std::int8_t>(r);
}
inline void imul16(Cpu& c, std::uint16_t b) {
    const std::int32_t r = static_cast<std::int16_t>(c.r[AX]) * static_cast<std::int32_t>(static_cast<std::int16_t>(b));
    c.r[AX] = static_cast<std::uint16_t>(r);
    c.r[DX] = static_cast<std::uint16_t>(static_cast<std::uint32_t>(r) >> 16);
    c.cf = c.of = r != static_cast<std::int16_t>(r);
}
inline std::uint16_t imul3(Cpu& c, std::uint16_t a, std::uint16_t b) {
    const std::int32_t r = static_cast<std::int16_t>(a) * static_cast<std::int32_t>(static_cast<std::int16_t>(b));
    c.cf = c.of = r != static_cast<std::int16_t>(r);
    return static_cast<std::uint16_t>(r);
}

// Division returns false on a divide error (zero divisor or quotient
// overflow); the caller then raises interrupt 0. Flags are undefined and left alone.
inline bool div8(Cpu& c, std::uint8_t b) {
    if (b == 0) return false;
    const unsigned q = c.r[AX] / b;
    if (q > 0xFF) return false;
    c.r[AX] = static_cast<std::uint16_t>(((c.r[AX] % b) << 8) | q);
    return true;
}
inline bool div16(Cpu& c, std::uint16_t b) {
    if (b == 0) return false;
    const std::uint32_t n = (static_cast<std::uint32_t>(c.r[DX]) << 16) | c.r[AX];
    const std::uint32_t q = n / b;
    if (q > 0xFFFF) return false;
    c.r[AX] = static_cast<std::uint16_t>(q);
    c.r[DX] = static_cast<std::uint16_t>(n % b);
    return true;
}
inline bool idiv8(Cpu& c, std::uint8_t b) {
    if (b == 0) return false;
    const int n = static_cast<std::int16_t>(c.r[AX]);
    const int d = static_cast<std::int8_t>(b);
    const int q = n / d;
    if (q > 127 || q < -128) return false;
    c.r[AX] = static_cast<std::uint16_t>(((n % d) & 0xFF) << 8 | (q & 0xFF));
    return true;
}
inline bool idiv16(Cpu& c, std::uint16_t b) {
    if (b == 0) return false;
    const std::int32_t n = static_cast<std::int32_t>((static_cast<std::uint32_t>(c.r[DX]) << 16) | c.r[AX]);
    const std::int32_t d = static_cast<std::int16_t>(b);
    if (n == INT32_MIN && d == -1) return false;
    const std::int32_t q = n / d;
    if (q > 32767 || q < -32768) return false;
    c.r[AX] = static_cast<std::uint16_t>(q);
    c.r[DX] = static_cast<std::uint16_t>(n % d);
    return true;
}

inline void cbw(Cpu& c) { c.r[AX] = static_cast<std::uint16_t>(static_cast<std::int8_t>(c.r[AX] & 0xFF)); }
inline void cwd(Cpu& c) { c.r[DX] = (c.r[AX] & 0x8000) ? 0xFFFF : 0; }
inline void lahf(Cpu& c) { c.set8(4, static_cast<std::uint8_t>(c.pack() & 0xD5 | 0x02)); }
inline void sahf(Cpu& c) {
    const std::uint8_t ah = c.get8(4);
    c.cf = ah & 1, c.pf = ah & 4, c.af = ah & 0x10, c.zf = ah & 0x40, c.sf = ah & 0x80;
}

// BCD adjustments.
inline void daa(Cpu& c) {
    unsigned al = c.r[AX] & 0xFF;
    const unsigned old = al;
    const bool oldcf = c.cf;
    c.cf = false;
    if ((al & 0x0F) > 9 || c.af) {
        al += 6;
        c.cf = oldcf || al > 0xFF;
        c.af = true;
    } else {
        c.af = false;
    }
    if (old > 0x99 || oldcf) {
        al += 0x60;
        c.cf = true;
    }
    c.set8(0, static_cast<std::uint8_t>(al));
    szp<8>(c, al);
}
inline void das(Cpu& c) {
    unsigned al = c.r[AX] & 0xFF;
    const unsigned old = al;
    const bool oldcf = c.cf;
    c.cf = false;
    if ((al & 0x0F) > 9 || c.af) {
        c.cf = oldcf || al < 6;
        al -= 6;
        c.af = true;
    } else {
        c.af = false;
    }
    if (old > 0x99 || oldcf) {
        al -= 0x60;
        c.cf = true;
    }
    c.set8(0, static_cast<std::uint8_t>(al));
    szp<8>(c, al);
}
// AAA/AAS as on the 80286 and later: AH is adjusted by exactly one.
inline void aaa(Cpu& c) {
    if ((c.r[AX] & 0x0F) > 9 || c.af) {
        c.set8(0, static_cast<std::uint8_t>((c.get8(0) + 6) & 0x0F));
        c.set8(4, static_cast<std::uint8_t>(c.get8(4) + 1));
        c.af = c.cf = true;
    } else {
        c.af = c.cf = false;
        c.r[AX] &= 0xFF0F;
    }
}
inline void aas(Cpu& c) {
    if ((c.r[AX] & 0x0F) > 9 || c.af) {
        c.set8(0, static_cast<std::uint8_t>((c.get8(0) - 6) & 0x0F));
        c.set8(4, static_cast<std::uint8_t>(c.get8(4) - 1));
        c.af = c.cf = true;
    } else {
        c.af = c.cf = false;
        c.r[AX] &= 0xFF0F;
    }
}
inline bool aam(Cpu& c, std::uint8_t base) {
    if (base == 0) return false;
    const unsigned al = c.r[AX] & 0xFF;
    c.r[AX] = static_cast<std::uint16_t>(((al / base) << 8) | (al % base));
    szp<8>(c, c.r[AX]);
    return true;
}
inline void aad(Cpu& c, std::uint8_t base) {
    const unsigned al = (c.get8(0) + c.get8(4) * base) & 0xFF;
    c.r[AX] = static_cast<std::uint16_t>(al);
    szp<8>(c, al);
}

// Condition codes for Jcc, in opcode order 70h..7Fh.
inline bool cond(const Cpu& c, int cc) {
    bool r;
    switch (cc >> 1) {
        case 0: r = c.of; break;
        case 1: r = c.cf; break;
        case 2: r = c.zf; break;
        case 3: r = c.cf || c.zf; break;
        case 4: r = c.sf; break;
        case 5: r = c.pf; break;
        case 6: r = c.sf != c.of; break;
        default: r = c.zf || (c.sf != c.of); break;
    }
    return (cc & 1) ? !r : r;
}

}  // namespace trek::dos::op

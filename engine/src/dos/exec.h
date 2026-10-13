#pragma once

// Helpers used by both recompiled code and the interpreter for instructions
// that touch more than registers: string operations.

#include "dos/machine.h"
#include "dos/ops.h"

namespace trek::dos::exec {

enum Rep { kNoRep = 0, kRepE = 1, kRepNE = 2 };

template <int W>
inline std::uint16_t load(Machine& m, std::uint16_t seg, std::uint16_t off) {
    if constexpr (W == 8) return m.rd8(seg, off);
    else return m.rd16(seg, off);
}
template <int W>
inline void store(Machine& m, std::uint16_t seg, std::uint16_t off, std::uint16_t v) {
    if constexpr (W == 8) m.wr8(seg, off, static_cast<std::uint8_t>(v));
    else m.wr16(seg, off, v);
}
template <int W>
inline std::uint16_t step_of(const Cpu& c) {
    return static_cast<std::uint16_t>(c.df ? -(W / 8) : (W / 8));
}

template <int W>
inline void movs(Machine& m, int seg, int rep) {
    Cpu& c = m.cpu;
    const std::uint16_t d = step_of<W>(c);
    if (rep && c.r[CX] == 0) return;
    do {
        store<W>(m, c.s[ES], c.r[DI], load<W>(m, c.s[seg], c.r[SI]));
        c.r[SI] = static_cast<std::uint16_t>(c.r[SI] + d);
        c.r[DI] = static_cast<std::uint16_t>(c.r[DI] + d);
    } while (rep && --c.r[CX]);
}

template <int W>
inline void stos(Machine& m, int rep) {
    Cpu& c = m.cpu;
    const std::uint16_t d = step_of<W>(c);
    if (rep && c.r[CX] == 0) return;
    const std::uint16_t v = W == 8 ? c.r[AX] & 0xFF : c.r[AX];
    do {
        store<W>(m, c.s[ES], c.r[DI], v);
        c.r[DI] = static_cast<std::uint16_t>(c.r[DI] + d);
    } while (rep && --c.r[CX]);
}

template <int W>
inline void lods(Machine& m, int seg, int rep) {
    Cpu& c = m.cpu;
    const std::uint16_t d = step_of<W>(c);
    if (rep && c.r[CX] == 0) return;
    do {
        const std::uint16_t v = load<W>(m, c.s[seg], c.r[SI]);
        if constexpr (W == 8) c.set8(0, static_cast<std::uint8_t>(v));
        else c.r[AX] = v;
        c.r[SI] = static_cast<std::uint16_t>(c.r[SI] + d);
    } while (rep && --c.r[CX]);
}

template <int W>
inline void cmps(Machine& m, int seg, int rep) {
    Cpu& c = m.cpu;
    const std::uint16_t d = step_of<W>(c);
    if (rep && c.r[CX] == 0) return;
    do {
        op::sub<W>(c, load<W>(m, c.s[seg], c.r[SI]), load<W>(m, c.s[ES], c.r[DI]));
        c.r[SI] = static_cast<std::uint16_t>(c.r[SI] + d);
        c.r[DI] = static_cast<std::uint16_t>(c.r[DI] + d);
        if (!rep) return;
    } while (--c.r[CX] && (rep == kRepE ? c.zf : !c.zf));
}

template <int W>
inline void scas(Machine& m, int rep) {
    Cpu& c = m.cpu;
    const std::uint16_t d = step_of<W>(c);
    if (rep && c.r[CX] == 0) return;
    const std::uint16_t a = W == 8 ? c.r[AX] & 0xFF : c.r[AX];
    do {
        op::sub<W>(c, a, load<W>(m, c.s[ES], c.r[DI]));
        c.r[DI] = static_cast<std::uint16_t>(c.r[DI] + d);
        if (!rep) return;
    } while (--c.r[CX] && (rep == kRepE ? c.zf : !c.zf));
}

}  // namespace trek::dos::exec

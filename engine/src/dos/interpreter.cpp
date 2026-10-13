// Interpreter for code the recompiler did not find (reached only through
// pointers computed at run time) and for self-modifying stubs. It decodes the
// same instruction set as tools/trekre/src/trekre/x86.py and executes through
// the same semantic helpers as recompiled code.
#include "dos/exec.h"
#include "dos/machine.h"
#include "dos/ops.h"

namespace trek::dos {

namespace {

struct Operand {
    bool is_reg = false;
    int n = 0;  // register number (width-specific numbering)
    std::uint16_t seg = 0, off = 0;
};

class Interp {
public:
    explicit Interp(Machine& m) : m_(m), c_(m.cpu), cs_(m.cpu.s[CS]), ip_(m.cpu.ip()) {}

    bool step();

private:
    std::uint8_t u8() { return m_.rd8(cs_, ip_++); }
    std::uint16_t u16() {
        const std::uint16_t lo = u8();
        return static_cast<std::uint16_t>(lo | u8() << 8);
    }
    std::uint16_t s8() { return static_cast<std::uint16_t>(static_cast<std::int8_t>(u8())); }

    // Decodes a ModRM byte; returns the reg field and fills the r/m operand.
    int modrm(Operand& rm) {
        const std::uint8_t b = u8();
        const int mod = b >> 6, reg = (b >> 3) & 7, r = b & 7;
        if (mod == 3) {
            rm.is_reg = true;
            rm.n = r;
            return reg;
        }
        std::uint16_t off = 0;
        int def = DS;
        if (mod == 0 && r == 6) {
            off = u16();
        } else {
            static constexpr int base[8] = {BX, BX, BP, BP, SI, DI, BP, BX};
            static constexpr int index[8] = {SI, DI, SI, DI, -1, -1, -1, -1};
            off = c_.r[base[r]];
            if (index[r] >= 0) off = static_cast<std::uint16_t>(off + c_.r[index[r]]);
            if (base[r] == BP) def = SS;
            if (mod == 1) off = static_cast<std::uint16_t>(off + s8());
            if (mod == 2) off = static_cast<std::uint16_t>(off + u16());
        }
        rm.seg = c_.s[seg_ >= 0 ? seg_ : def];
        rm.off = off;
        return reg;
    }
    template <int W>
    std::uint16_t get(const Operand& o) {
        if (o.is_reg) return W == 8 ? c_.get8(o.n) : c_.r[o.n];
        return exec::load<W>(m_, o.seg, o.off);
    }
    template <int W>
    void put(const Operand& o, std::uint16_t v) {
        if (o.is_reg) {
            if constexpr (W == 8) c_.set8(o.n, static_cast<std::uint8_t>(v));
            else c_.r[o.n] = v;
        } else {
            exec::store<W>(m_, o.seg, o.off, v);
        }
    }
    template <int W>
    std::uint16_t reg(int n) const { return W == 8 ? c_.get8(n) : c_.r[n]; }
    template <int W>
    void set_reg(int n, std::uint16_t v) {
        if constexpr (W == 8) c_.set8(n, static_cast<std::uint8_t>(v));
        else c_.r[n] = v;
    }

    template <int W>
    std::uint16_t alu(int op, std::uint16_t a, std::uint16_t b) {
        switch (op) {
            case 0: return op::add<W>(c_, a, b);
            case 1: return op::or_<W>(c_, a, b);
            case 2: return op::adc<W>(c_, a, b);
            case 3: return op::sbb<W>(c_, a, b);
            case 4: return op::and_<W>(c_, a, b);
            case 5: return op::sub<W>(c_, a, b);
            case 6: return op::xor_<W>(c_, a, b);
            default: op::sub<W>(c_, a, b); return a;  // cmp
        }
    }

    template <int W>
    bool grp3(int sub, const Operand& rm) {
        const std::uint16_t v = get<W>(rm);
        switch (sub) {
            case 0:
            case 1: op::and_<W>(c_, v, W == 8 ? u8() : u16()); return true;
            case 2: put<W>(rm, static_cast<std::uint16_t>(~v)); return true;
            case 3: put<W>(rm, op::neg<W>(c_, v)); return true;
            case 4: W == 8 ? op::mul8(c_, static_cast<std::uint8_t>(v)) : op::mul16(c_, v); return true;
            case 5: W == 8 ? op::imul8(c_, static_cast<std::uint8_t>(v)) : op::imul16(c_, v); return true;
            case 6: return W == 8 ? op::div8(c_, static_cast<std::uint8_t>(v)) : op::div16(c_, v);
            default: return W == 8 ? op::idiv8(c_, static_cast<std::uint8_t>(v)) : op::idiv16(c_, v);
        }
    }

    std::uint32_t next() const { return Machine::lin(cs_, ip_); }
    void divide_error() { m_.interrupt(0, next()); }

    Machine& m_;
    Cpu& c_;
    std::uint16_t cs_, ip_;
    int seg_ = -1;
    int rep_ = exec::kNoRep;
};

bool Interp::step() {
    std::uint8_t b;
    for (int i = 0;; ++i) {
        if (i > 14) return false;
        b = u8();
        if (b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E) seg_ = (b >> 3) & 3;
        else if (b == 0xF2) rep_ = exec::kRepNE;
        else if (b == 0xF3) rep_ = exec::kRepE;
        else if (b != 0xF0) break;
    }
    Operand rm;
    // Default: fall through to the next instruction.
    auto finish = [this] { c_.pc = next(); };

    if (b < 0x40 && (b & 7) < 6) {
        const int op = b >> 3, kind = b & 7;
        // CMP (op 7) must not write: a store to video memory has side effects.
        if (kind == 0 || kind == 2) {
            const int r = modrm(rm);
            if (kind == 0) {
                const std::uint16_t v = alu<8>(op, get<8>(rm), reg<8>(r));
                if (op != 7) put<8>(rm, v);
            } else {
                set_reg<8>(r, alu<8>(op, reg<8>(r), get<8>(rm)));
            }
        } else if (kind == 1 || kind == 3) {
            const int r = modrm(rm);
            if (kind == 1) {
                const std::uint16_t v = alu<16>(op, get<16>(rm), reg<16>(r));
                if (op != 7) put<16>(rm, v);
            } else {
                set_reg<16>(r, alu<16>(op, reg<16>(r), get<16>(rm)));
            }
        } else if (kind == 4) {
            c_.set8(0, static_cast<std::uint8_t>(alu<8>(op, c_.get8(0), u8())));
        } else {
            c_.r[AX] = alu<16>(op, c_.r[AX], u16());
        }
        finish();
        return true;
    }
    switch (b) {
        case 0x06: case 0x0E: case 0x16: case 0x1E: m_.push(c_.s[b >> 3]); break;
        case 0x07: case 0x17: case 0x1F: c_.s[b >> 3] = m_.pop(); break;
        case 0x27: op::daa(c_); break;
        case 0x2F: op::das(c_); break;
        case 0x37: op::aaa(c_); break;
        case 0x3F: op::aas(c_); break;
        case 0x60: {
            const std::uint16_t sp = c_.r[SP];
            for (int r = 0; r < 8; ++r) m_.push(r == SP ? sp : c_.r[r]);
            break;
        }
        case 0x61:
            for (int r = 7; r >= 0; --r) {
                const std::uint16_t v = m_.pop();
                if (r != SP) c_.r[r] = v;
            }
            break;
        case 0x68: m_.push(u16()); break;
        case 0x6A: m_.push(s8()); break;
        case 0x69: case 0x6B: {
            const int r = modrm(rm);
            const std::uint16_t a = get<16>(rm);
            const std::uint16_t imm = b == 0x69 ? u16() : s8();
            c_.r[r] = op::imul3(c_, a, imm);
            break;
        }
        case 0x80: case 0x81: case 0x82: case 0x83: {
            const int sub = modrm(rm);
            if (b == 0x81 || b == 0x83) {
                const std::uint16_t imm = b == 0x81 ? u16() : s8();
                const std::uint16_t r = alu<16>(sub, get<16>(rm), imm);
                if (sub != 7) put<16>(rm, r);
            } else {
                const std::uint16_t r = alu<8>(sub, get<8>(rm), u8());
                if (sub != 7) put<8>(rm, r);
            }
            break;
        }
        case 0x84: { const int r = modrm(rm); op::and_<8>(c_, get<8>(rm), reg<8>(r)); break; }
        case 0x85: { const int r = modrm(rm); op::and_<16>(c_, get<16>(rm), reg<16>(r)); break; }
        case 0x86: { const int r = modrm(rm); const auto t = get<8>(rm); put<8>(rm, reg<8>(r)); set_reg<8>(r, t); break; }
        case 0x87: { const int r = modrm(rm); const auto t = get<16>(rm); put<16>(rm, reg<16>(r)); set_reg<16>(r, t); break; }
        case 0x88: { const int r = modrm(rm); put<8>(rm, reg<8>(r)); break; }
        case 0x89: { const int r = modrm(rm); put<16>(rm, reg<16>(r)); break; }
        case 0x8A: { const int r = modrm(rm); set_reg<8>(r, get<8>(rm)); break; }
        case 0x8B: { const int r = modrm(rm); set_reg<16>(r, get<16>(rm)); break; }
        case 0x8C: { const int r = modrm(rm); put<16>(rm, c_.s[r & 3]); break; }
        case 0x8D: { const int r = modrm(rm); if (rm.is_reg) return false; c_.r[r] = rm.off; break; }
        case 0x8E: { const int r = modrm(rm); c_.s[r & 3] = get<16>(rm); break; }
        case 0x8F: { modrm(rm); put<16>(rm, m_.pop()); break; }
        case 0x90: break;
        case 0x98: op::cbw(c_); break;
        case 0x99: op::cwd(c_); break;
        case 0x9A: {
            const std::uint16_t off = u16(), seg = u16();
            m_.push(c_.s[CS]);
            m_.push(ip_);
            m_.far_jump(seg, off);
            return true;
        }
        case 0x9B: break;
        case 0x9C: m_.push(c_.pack()); break;
        case 0x9D: c_.unpack(m_.pop()); break;
        case 0x9E: op::sahf(c_); break;
        case 0x9F: op::lahf(c_); break;
        case 0xA0: c_.set8(0, m_.rd8(c_.s[seg_ >= 0 ? seg_ : DS], u16())); break;
        case 0xA1: c_.r[AX] = m_.rd16(c_.s[seg_ >= 0 ? seg_ : DS], u16()); break;
        case 0xA2: m_.wr8(c_.s[seg_ >= 0 ? seg_ : DS], u16(), c_.get8(0)); break;
        case 0xA3: m_.wr16(c_.s[seg_ >= 0 ? seg_ : DS], u16(), c_.r[AX]); break;
        case 0xA4: exec::movs<8>(m_, seg_ >= 0 ? seg_ : DS, rep_ != 0); break;
        case 0xA5: exec::movs<16>(m_, seg_ >= 0 ? seg_ : DS, rep_ != 0); break;
        case 0xA6: exec::cmps<8>(m_, seg_ >= 0 ? seg_ : DS, rep_); break;
        case 0xA7: exec::cmps<16>(m_, seg_ >= 0 ? seg_ : DS, rep_); break;
        case 0xA8: op::and_<8>(c_, c_.get8(0), u8()); break;
        case 0xA9: op::and_<16>(c_, c_.r[AX], u16()); break;
        case 0xAA: exec::stos<8>(m_, rep_ != 0); break;
        case 0xAB: exec::stos<16>(m_, rep_ != 0); break;
        case 0xAC: exec::lods<8>(m_, seg_ >= 0 ? seg_ : DS, rep_ != 0); break;
        case 0xAD: exec::lods<16>(m_, seg_ >= 0 ? seg_ : DS, rep_ != 0); break;
        case 0xAE: exec::scas<8>(m_, rep_); break;
        case 0xAF: exec::scas<16>(m_, rep_); break;
        case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
            const int sub = modrm(rm);
            const unsigned count = (b == 0xC0 || b == 0xC1) ? u8() : (b == 0xD0 || b == 0xD1) ? 1 : c_.get8(1);
            if (b & 1) put<16>(rm, op::shift<16>(c_, sub, get<16>(rm), count));
            else put<8>(rm, op::shift<8>(c_, sub, get<8>(rm), count));
            break;
        }
        case 0xC2: { const std::uint16_t n = u16(); m_.near_return(n); return true; }
        case 0xC3: m_.near_return(0); return true;
        case 0xC4: case 0xC5: {
            const int r = modrm(rm);
            if (rm.is_reg) return false;
            c_.r[r] = m_.rd16(rm.seg, rm.off);
            c_.s[b == 0xC4 ? ES : DS] = m_.rd16(rm.seg, static_cast<std::uint16_t>(rm.off + 2));
            break;
        }
        case 0xC6: { modrm(rm); put<8>(rm, u8()); break; }
        case 0xC7: { modrm(rm); put<16>(rm, u16()); break; }
        case 0xC8: {
            const std::uint16_t size = u16();
            const int level = u8() & 31;
            m_.push(c_.r[BP]);
            const std::uint16_t frame = c_.r[SP];
            for (int i = 1; i < level; ++i) {
                c_.r[BP] = static_cast<std::uint16_t>(c_.r[BP] - 2);
                m_.push(m_.rd16(c_.s[SS], c_.r[BP]));
            }
            if (level > 0) m_.push(frame);
            c_.r[BP] = frame;
            c_.r[SP] = static_cast<std::uint16_t>(c_.r[SP] - size);
            break;
        }
        case 0xC9: c_.r[SP] = c_.r[BP]; c_.r[BP] = m_.pop(); break;
        case 0xCA: { const std::uint16_t n = u16(); m_.far_return(n); return true; }
        case 0xCB: m_.far_return(0); return true;
        case 0xCC: m_.interrupt(3, next()); return true;
        case 0xCD: { const std::uint8_t n = u8(); m_.interrupt(n, next()); return true; }
        case 0xCE: if (c_.of) { m_.interrupt(4, next()); return true; } break;
        case 0xCF: m_.iret(); return true;
        case 0xD4: { const std::uint8_t base = u8(); if (!op::aam(c_, base)) { divide_error(); return true; } break; }
        case 0xD5: op::aad(c_, u8()); break;
        case 0xD6: c_.set8(0, c_.cf ? 0xFF : 0); break;
        case 0xD7: c_.set8(0, m_.rd8(c_.s[seg_ >= 0 ? seg_ : DS], static_cast<std::uint16_t>(c_.r[BX] + c_.get8(0)))); break;
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: {
            const std::uint16_t d = s8();
            bool take;
            if (b == 0xE3) {
                take = c_.r[CX] == 0;
            } else {
                --c_.r[CX];
                take = c_.r[CX] != 0 && (b == 0xE2 || (b == 0xE1 ? c_.zf : !c_.zf));
            }
            if (take) ip_ = static_cast<std::uint16_t>(ip_ + d);
            break;
        }
        case 0xE4: c_.set8(0, m_.in8(u8())); break;
        case 0xE5: c_.r[AX] = m_.in16(u8()); break;
        case 0xE6: m_.out8(u8(), c_.get8(0)); break;
        case 0xE7: m_.out16(u8(), c_.r[AX]); break;
        case 0xEC: c_.set8(0, m_.in8(c_.r[DX])); break;
        case 0xED: c_.r[AX] = m_.in16(c_.r[DX]); break;
        case 0xEE: m_.out8(c_.r[DX], c_.get8(0)); break;
        case 0xEF: m_.out16(c_.r[DX], c_.r[AX]); break;
        case 0xE8: {
            const std::uint16_t d = u16();
            m_.push(ip_);
            m_.near_jump(static_cast<std::uint16_t>(ip_ + d));
            return true;
        }
        case 0xE9: { const std::uint16_t d = u16(); ip_ = static_cast<std::uint16_t>(ip_ + d); break; }
        case 0xEA: { const std::uint16_t off = u16(), seg = u16(); m_.far_jump(seg, off); return true; }
        case 0xEB: { const std::uint16_t d = s8(); ip_ = static_cast<std::uint16_t>(ip_ + d); break; }
        case 0xF4: m_.halt(); break;
        case 0xF5: c_.cf = !c_.cf; break;
        case 0xF6: case 0xF7: {
            const int sub = modrm(rm);
            const bool ok = b == 0xF6 ? grp3<8>(sub, rm) : grp3<16>(sub, rm);
            if (!ok) { divide_error(); return true; }
            break;
        }
        case 0xF8: c_.cf = false; break;
        case 0xF9: c_.cf = true; break;
        case 0xFA: c_.ifl = false; break;
        case 0xFB: c_.ifl = true; break;
        case 0xFC: c_.df = false; break;
        case 0xFD: c_.df = true; break;
        case 0xFE: {
            const int sub = modrm(rm);
            if (sub > 1) return false;
            put<8>(rm, sub == 0 ? op::inc<8>(c_, get<8>(rm)) : op::dec<8>(c_, get<8>(rm)));
            break;
        }
        case 0xFF: {
            const int sub = modrm(rm);
            switch (sub) {
                case 0: put<16>(rm, op::inc<16>(c_, get<16>(rm))); break;
                case 1: put<16>(rm, op::dec<16>(c_, get<16>(rm))); break;
                case 2: { const std::uint16_t t = get<16>(rm); m_.push(ip_); m_.near_jump(t); return true; }
                case 3: {
                    if (rm.is_reg) return false;
                    const std::uint16_t off = m_.rd16(rm.seg, rm.off);
                    const std::uint16_t seg = m_.rd16(rm.seg, static_cast<std::uint16_t>(rm.off + 2));
                    m_.push(c_.s[CS]);
                    m_.push(ip_);
                    m_.far_jump(seg, off);
                    return true;
                }
                case 4: m_.near_jump(get<16>(rm)); return true;
                case 5: {
                    if (rm.is_reg) return false;
                    const std::uint16_t off = m_.rd16(rm.seg, rm.off);
                    m_.far_jump(m_.rd16(rm.seg, static_cast<std::uint16_t>(rm.off + 2)), off);
                    return true;
                }
                case 6: m_.push(get<16>(rm)); break;
                default: return false;
            }
            break;
        }
        default:
            if (b >= 0x40 && b < 0x48) { c_.r[b & 7] = op::inc<16>(c_, c_.r[b & 7]); break; }
            if (b >= 0x48 && b < 0x50) { c_.r[b & 7] = op::dec<16>(c_, c_.r[b & 7]); break; }
            if (b >= 0x50 && b < 0x58) {
                // PUSH SP stores the value before the decrement (80286 behaviour).
                const std::uint16_t v = c_.r[b & 7];
                m_.push(v);
                break;
            }
            if (b >= 0x58 && b < 0x60) { c_.r[b & 7] = m_.pop(); break; }
            if (b >= 0x70 && b < 0x80) {
                const std::uint16_t d = s8();
                if (op::cond(c_, b - 0x70)) ip_ = static_cast<std::uint16_t>(ip_ + d);
                break;
            }
            if (b >= 0x91 && b <= 0x97) { std::swap(c_.r[AX], c_.r[b & 7]); break; }
            if (b >= 0xB0 && b < 0xB8) { c_.set8(b & 7, u8()); break; }
            if (b >= 0xB8 && b < 0xC0) { c_.r[b & 7] = u16(); break; }
            if (b >= 0xD8 && b <= 0xDF) { modrm(rm); break; }  // no FPU: ESC is a no-op
            return false;
    }
    finish();
    return true;
}

}  // namespace

bool Machine::step() { return Interp(*this).step(); }

}  // namespace trek::dos

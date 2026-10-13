#include "dos/machine.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "dos/dos_file.h"
#include "dos/rom_font.h"

namespace trek::dos {

namespace {
constexpr std::uint16_t kEnvSeg = 0x0F00;
constexpr std::uint16_t kFontOff = 0xFA6E;       // IBM BIOS location of the 8x8 font
constexpr std::uint16_t kFontHighOff = 0xF000;   // characters 128-255 (blank)
constexpr double kTicksPerMs = 1193182.0 / 65536.0 / 1000.0;
}  // namespace

Machine::Machine(const Program& program, FileSystem fs)
    : program_(program), fs_(std::move(fs)), mem_(kMemSize + 16, 0) {
    reset();
    image_lo_ = program_.load_segment * 16u;
    image_size_ = static_cast<std::uint32_t>(program_.image.size());
    written_.assign(image_size_, false);
}

void Machine::watched_write(std::uint32_t a, std::uint8_t v) {
    const std::uint32_t who = lin(cpu.s[CS], cpu.ip());
    if (warned_.insert(static_cast<int>(0x20000000u | ((who ^ (a & 0xFFFFF000u)) & 0x0FFFFFFF))).second)
        SDL_Log("write %02X to %05X by block/insn near %04X:%04X (pc %05X), DS=%04X ES=%04X SI=%04X DI=%04X CX=%04X", v,
                a, cpu.s[CS], cpu.ip(), cpu.pc, cpu.s[DS], cpu.s[ES], cpu.r[SI], cpu.r[DI], cpu.r[CX]);
}

void Machine::self_modified(std::uint32_t a) {
    // Recompiled code would not see the change. Known cases are listed in
    // tools/trekre/src/trekre/hints.py (DYNAMIC) and run interpreted.
    if (warned_.insert(static_cast<int>(0x40000000u | a)).second)
        SDL_Log("self-modifying write to recompiled code at %05X (%04X:%04X)", a, cpu.s[CS], cpu.ip());
}

Machine::~Machine() = default;

void Machine::reset() {
    std::fill(mem_.begin(), mem_.end(), 0);
    // Interrupt vectors all point at per-interrupt stubs in the BIOS segment;
    // reaching a stub runs the native service (see run_stub).
    for (int n = 0; n < 256; ++n) {
        const std::uint16_t off = static_cast<std::uint16_t>(kStubBase + 4 * n);
        wr16(0, static_cast<std::uint16_t>(n * 4), off);
        wr16(0, static_cast<std::uint16_t>(n * 4 + 2), kBiosSeg);
        mem_[lin(kBiosSeg, off)] = 0xCF;  // IRET, should anything execute it
    }
    std::memcpy(&mem_[lin(kBiosSeg, kFontOff)], kRomFont8x8, sizeof kRomFont8x8);
    wr16(0, 0x1F * 4, kFontHighOff);
    wr16(0, 0x1F * 4 + 2, kBiosSeg);
    wr16(0, 0x43 * 4, kFontOff);
    wr16(0, 0x43 * 4 + 2, kBiosSeg);

    // BIOS data area.
    wr16(0x40, 0x10, 0x0021);  // equipment: floppy, 80x25 colour (EGA reports via INT 10h/12h)
    wr16(0x40, 0x13, 640);     // KB of conventional memory
    mem_[0x449] = 3;           // video mode
    wr16(0x40, 0x4A, 80);
    wr16(0x40, 0x63, 0x3D4);
    mem_[0x484] = 24;
    wr16(0x40, 0x85, 14);
    mem_[0x487] = 0x60;
    mem_[0x488] = 0x09;
    mem_[0xFFFFE] = 0xFF;      // machine ID: PC

    // Environment and PSP, then the program image above them.
    const char env[] = "COMSPEC=C:\\COMMAND.COM\0PATH=C:\\\0\0\x01\0C:\\TREK.EXE";
    std::memcpy(&mem_[lin(kEnvSeg, 0)], env, sizeof env);
    const std::uint16_t psp = static_cast<std::uint16_t>(program_.load_segment - 0x10);
    wr16(psp, 0x00, 0x20CD);   // INT 20h
    wr16(psp, 0x02, 0xA000);   // first segment beyond our memory
    mem_[lin(psp, 0x05)] = 0x9A;  // CP/M-style far call, not the 0xC3 a debugger would plant
    wr16(psp, 0x2C, kEnvSeg);
    mem_[lin(psp, 0x80)] = 0;
    mem_[lin(psp, 0x81)] = 0x0D;
    std::memcpy(&mem_[lin(program_.load_segment, 0)], program_.image.data(), program_.image.size());

    cpu = Cpu{};
    cpu.s[DS] = cpu.s[ES] = psp;
    cpu.s[SS] = program_.ss;
    cpu.r[SP] = program_.sp;
    far_jump(program_.cs, program_.ip);
    ega_.set_mode(3, true);
    state_ = State::Running;
    keys_.clear();
    files_.clear();
}

void Machine::fail(std::string why) {
    if (state_ != State::Running) return;
    state_ = State::Faulted;
    fault_ = std::move(why);
    budget = 0;
    SDL_Log("machine fault: %s", fault_.c_str());
}

bool Machine::is_block(std::uint32_t pc) const {
    return std::binary_search(program_.blocks.begin(), program_.blocks.end(), pc);
}

const Program::Segment* Machine::segment_for(std::uint32_t pc) const {
    for (const auto& s : program_.segments)
        if (pc >= s.begin && pc < s.end) return &s;
    return nullptr;
}

std::vector<std::uint32_t> Machine::trace_tail() const {
    std::vector<std::uint32_t> out;
    const std::size_t n = std::min(trace_pos_, kTraceSize);
    for (std::size_t i = trace_pos_ - n; i < trace_pos_; ++i) out.push_back(trace_[i % kTraceSize]);
    return out;
}

Machine::State Machine::run(std::int64_t slice, double now_ms) {
    if (state_ != State::Running) return state_;
    now_ms_ = clock_override_ ? *clock_override_ : now_ms;
    if (start_ms_ < 0) start_ms_ = now_ms_;
    sync_ticks();
    deliver_timer();
    waiting_key_ = false;
    budget = slice;
    const std::uint32_t stub_lo = lin(kBiosSeg, kStubBase), stub_hi = stub_lo + 4 * 256;
    while (budget > 0 && state_ == State::Running) {
        const std::uint32_t pc = cpu.pc;
        if (pc >= stub_lo && pc < stub_hi) {
            if (!run_stub(static_cast<int>((pc - stub_lo) / 4))) break;
            --budget;
            continue;
        }
        if (const auto* seg = segment_for(pc); seg && !interpret_only_ && is_block(pc)) {
            const std::int64_t before = budget;
            seg->run(*this);
            blocks_run_ += static_cast<std::uint64_t>(before - budget);
            continue;
        }
        // Not recompiled: interpret until we reach a block again.
        const std::uint32_t image_lo = program_.load_segment * 16u, image_hi = image_lo + program_.image.size();
        if (pc < image_lo || pc >= image_hi) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "execution left the program image at %04X:%04X", cpu.s[CS], cpu.ip());
            fail(buf);
            break;
        }
        if (!interpret_only_) {
            ++interpreted_[pc];
            interpreted_cs_[pc] = cpu.s[CS];
        }
        do {
            if (tracing) {
                // Report block entries like recompiled code does; mark other instructions.
                if (is_block(cpu.pc)) {
                    if (!trace(cpu.pc)) break;
                } else if (!interpret_only_) {
                    trace(cpu.pc | 0x80000000u);
                }
            }
            if (!step()) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "cannot decode instruction at %04X:%04X", cpu.s[CS], cpu.ip());
                fail(buf);
                break;
            }
            --budget;
        } while (budget > 0 && state_ == State::Running && (interpret_only_ || !is_block(cpu.pc)) &&
                 !(cpu.pc >= stub_lo && cpu.pc < stub_hi) && cpu.pc - image_lo < program_.image.size());
    }
    return state_;
}

void Machine::interrupt(std::uint8_t n, std::uint32_t ret_pc) {
    push(cpu.pack());
    cpu.ifl = cpu.tf = false;
    push(cpu.s[CS]);
    push(static_cast<std::uint16_t>(ret_pc - cpu.s[CS] * 16u));
    far_jump(rd16(0, static_cast<std::uint16_t>(n * 4 + 2)), rd16(0, static_cast<std::uint16_t>(n * 4)));
    budget -= 1;
}

bool Machine::run_stub(int n) {
    bool done = true;
    switch (n) {
        case 0x00: fail("divide error (INT 0) with no handler installed"); return true;
        case 0x10: done = int10(); break;
        case 0x11: cpu.r[AX] = rd16(0x40, 0x10); break;
        case 0x12: cpu.r[AX] = rd16(0x40, 0x13); break;
        case 0x15: cpu.cf = true; cpu.set8(4, 0x86); break;  // no AT services
        case 0x16: done = int16(); break;
        case 0x1A: done = int1a(); break;
        case 0x20: state_ = State::Exited; budget = 0; return true;
        case 0x21: done = int21(); break;
        case 0x33: cpu.r[AX] = 0; break;  // no mouse driver
        case 0x05: case 0x08: case 0x09: case 0x1B: case 0x1C: case 0x23: case 0x24: case 0x28: case 0x2F:
            break;
        default:
            if (warned_.insert(0x1000 + n).second) SDL_Log("unhandled INT %02Xh AX=%04X", n, cpu.r[AX]);
            break;
    }
    if (!done) return false;
    if (state_ != State::Running) return true;
    // Return as IRET would, but keep the CF/ZF results of the service.
    const bool cf = cpu.cf, zf = cpu.zf;
    iret();
    cpu.cf = cf, cpu.zf = zf;
    return true;
}

void Machine::read_ticks() {
    if (!deterministic_ticks_) return sync_ticks();
    ticks_ = static_cast<std::uint32_t>(tick_reads_++ / 64);
    std::memcpy(&mem_[0x46C], &ticks_, 4);
}

void Machine::sync_ticks() {
    if (deterministic_ticks_ || start_ms_ < 0) return;
    // Ticks since midnight: start at host local time, advance with host time.
    static const double midnight_offset = [] {
        const std::time_t t = std::time(nullptr);
        std::tm lt{};
#if defined(_WIN32)
        localtime_s(&lt, &t);
#else
        localtime_r(&t, &lt);
#endif
        return (lt.tm_hour * 3600.0 + lt.tm_min * 60.0 + lt.tm_sec) * 1000.0;
    }();
    const double base = clock_override_ ? 0.0 : midnight_offset;
    ticks_ = static_cast<std::uint32_t>((base + now_ms_ - start_ms_) * kTicksPerMs);
    std::memcpy(&mem_[0x46C], &ticks_, 4);  // little-endian hosts only (arm64, x86-64, wasm32)
}

void Machine::deliver_timer() {
    if (deterministic_ticks_ || ticks_ == delivered_ticks_) return;
    delivered_ticks_ = ticks_;
    if (!cpu.ifl) return;
    const auto hooked = [this](int n) {
        return rd16(0, static_cast<std::uint16_t>(n * 4 + 2)) != kBiosSeg;
    };
    // One interrupt per slice is enough for this game; handlers just count.
    if (hooked(0x1C)) interrupt(0x1C, cpu.pc);
    if (hooked(0x08)) interrupt(0x08, cpu.pc);
}

void Machine::key_down(std::uint8_t scan, std::uint8_t ascii) {
    if (keys_.size() < 16) keys_.push_back(static_cast<std::uint16_t>(scan << 8 | ascii));
}

std::uint8_t Machine::video_read(std::uint32_t a) {
    if (a < 0xB0000) return ega_.graphics() ? ega_.read(static_cast<std::uint16_t>(a - 0xA0000)) : 0xFF;
    return mem_[a];
}

void Machine::video_write(std::uint32_t a, std::uint8_t v) {
    if (a < 0xB0000) {
        if (ega_.graphics()) ega_.write(static_cast<std::uint16_t>(a - 0xA0000), v);
        return;
    }
    mem_[a] = v;
}

std::uint8_t Machine::in8(std::uint16_t port) {
    if (port >= 0x3B0 && port <= 0x3DF) return ega_.port_in(port);
    switch (port) {
        case 0x40:
        case 0x42: {
            // A counter that moves on every read keeps calibration loops finite.
            pit_counter_ = static_cast<std::uint16_t>(pit_counter_ - 37);
            const bool hi = pit_lo_written_;
            pit_lo_written_ = !pit_lo_written_;
            return hi ? static_cast<std::uint8_t>(pit_counter_ >> 8) : static_cast<std::uint8_t>(pit_counter_);
        }
        case 0x60: return keys_.empty() ? 0 : static_cast<std::uint8_t>(keys_.front() >> 8);
        case 0x61: {
            refresh_ = !refresh_;
            return static_cast<std::uint8_t>((port61_ & 0x0F) | (refresh_ ? 0x10 : 0) |
                                             (speaker_.on() ? 0x20 : 0));
        }
        case 0x201: return 0xF0;  // no joystick
        default: return 0xFF;
    }
}

void Machine::out8(std::uint16_t port, std::uint8_t v) {
    if (port >= 0x3B0 && port <= 0x3DF) {
        ega_.port_out(port, v);
        return;
    }
    switch (port) {
        case 0x43:
            pit_mode_ = v;
            pit_lo_written_ = false;
            break;
        case 0x42:
            if ((pit_mode_ >> 6) == 2) {
                if (!pit_lo_written_) {
                    pit_latch_lo_ = v;
                    pit_lo_written_ = true;
                } else {
                    pit2_divisor_ = static_cast<std::uint16_t>(pit_latch_lo_ | v << 8);
                    pit_lo_written_ = false;
                    speaker_.set((port61_ & 3) == 3, pit2_divisor_, now_ms_);
                }
            }
            break;
        case 0x61:
            port61_ = v;
            speaker_.set((v & 3) == 3, pit2_divisor_, now_ms_);
            break;
        default: break;  // PIC, PIT channel 0, etc.
    }
}

}  // namespace trek::dos

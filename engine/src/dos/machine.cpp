#include "dos/machine.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "dos/dos_file.h"
#include "dos/keymap.h"
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
    delay_active_ = false;
    delay_end_ms_ = 0.0;
    pit_read_hi_ = false;

    title_screen_entered_ = true;
    title_screen_passed_ = false;
    briefing_auto_answered_ = false;
    difficulty_auto_answered_ = false;
    password_auto_answered_ = false;

    // Shorten title music initial rest from 4.4 seconds to ~200 ms so fanfare plays promptly.
    const std::uint32_t music = lin(program_.load_segment + 0x1F7B, 0x06A9);
    if (music + 3 < mem_.size() && mem_[music] == 36 && mem_[music + 1] == 0 &&
        mem_[music + 2] == 4 && mem_[music + 3] == 0) {
        mem_[music] = 1;
        mem_[music + 2] = 1;
    }
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
        const std::uint32_t delay_pc = lin(program_.load_segment + 0x197F, 0x02E9);
        if (!deterministic_ticks_ && pc == delay_pc) {
            if (handle_delay()) break;
            continue;
        }
        const std::uint32_t welcome_pc = lin(program_.load_segment + 0x0be3, 0x44a1);
        if (pc == welcome_pc) {
            title_screen_entered_ = true;
            title_screen_passed_ = true;
        }
        const std::uint32_t briefing_pc = lin(program_.load_segment + 0x0be3, 0x44eb);
        if (pc == briefing_pc && startup_settings_.skip_briefing && !briefing_auto_answered_) {
            briefing_auto_answered_ = true;
            type_string("N\r");
        }
        const std::uint32_t skill_pc = lin(program_.load_segment + 0x0be3, 0x4579);
        if (pc == skill_pc && startup_settings_.default_difficulty >= 1 &&
            startup_settings_.default_difficulty <= 5 && !difficulty_auto_answered_) {
            difficulty_auto_answered_ = true;
            type_string(std::to_string(startup_settings_.default_difficulty) + "\r");
        }
        const std::uint32_t password_pc = lin(program_.load_segment + 0x0be3, 0x45b1);
        if (pc == password_pc && !startup_settings_.default_password.empty() && !password_auto_answered_) {
            password_auto_answered_ = true;
            type_string(startup_settings_.default_password + "\r");
        }

        const std::uint32_t restore_pc = lin(program_.load_segment + 0x0182, 0x6997);
        if (pc == restore_pc) {
            if (handle_restore()) continue;
        }
        const std::uint32_t save_pc = lin(program_.load_segment + 0x0182, 0x6154);
        if (pc == save_pc) {
            handle_save();
        }
        const std::uint32_t cmd_input_pc = lin(program_.load_segment + 0x0000, 0x15fd);
        if (pc == cmd_input_pc) {
            handle_cmd_input();
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
    if (hooked(0x1C)) {
        interrupt(0x1C, cpu.pc);
    }
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

bool Machine::handle_delay() {
    if (delay_active_) {
        if (now_ms_ < delay_end_ms_) {
            budget = 0;
            return true;
        }
        delay_active_ = false;
        far_return(2);
        return false;
    }
    const std::uint16_t ms = rd16(cpu.s[SS], static_cast<std::uint16_t>(cpu.r[SP] + 4));
    if (ms == 0) {
        far_return(2);
        return false;
    }
    delay_active_ = true;
    delay_end_ms_ = now_ms_ + ms;
    budget = 0;
    return true;
}

std::uint8_t Machine::in8(std::uint16_t port) {
    if (port >= 0x3B0 && port <= 0x3DF) return ega_.port_in(port);
    switch (port) {
        case 0x40:
        case 0x42: {
            // A counter that moves on every read keeps calibration loops finite.
            pit_counter_ = static_cast<std::uint16_t>(pit_counter_ - 37);
            const bool hi = pit_read_hi_;
            pit_read_hi_ = !pit_read_hi_;
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

bool Machine::is_at_command_prompt() const {
    if (state_ != State::Running) return false;
    return mem_[lin(cpu.s[DS], 0x151D)] == 1;
}

bool Machine::is_at_move_prompt() const {
    if (state_ != State::Running) return false;
    auto read16 = [this](std::uint16_t seg, std::uint16_t off) -> std::uint16_t {
        const std::uint32_t a = lin(seg, off);
        return static_cast<std::uint16_t>(mem_[a] | (mem_[(a + 1) & 0xFFFFF] << 8));
    };
    const std::uint16_t cs_182 = static_cast<std::uint16_t>(program_.load_segment + 0x0182);
    const std::uint16_t caller_ip = read16(cpu.s[SS], static_cast<std::uint16_t>(cpu.r[BP] + 2));
    const std::uint16_t caller_cs = read16(cpu.s[SS], static_cast<std::uint16_t>(cpu.r[BP] + 4));
    return caller_cs == cs_182 && caller_ip == 0x2585;
}

std::string Machine::current_move_input() const {
    if (!is_at_move_prompt()) return "";
    auto read16 = [this](std::uint16_t seg, std::uint16_t off) -> std::uint16_t {
        const std::uint32_t a = lin(seg, off);
        return static_cast<std::uint16_t>(mem_[a] | (mem_[(a + 1) & 0xFFFFF] << 8));
    };
    const std::uint16_t buf_off = read16(cpu.s[SS], static_cast<std::uint16_t>(cpu.r[BP] + 6));
    const std::uint16_t buf_seg = read16(cpu.s[SS], static_cast<std::uint16_t>(cpu.r[BP] + 8));
    const std::uint8_t len = mem_[lin(buf_seg, buf_off)];
    std::string s;
    s.reserve(len);
    for (std::uint8_t i = 0; i < len; ++i) {
        s.push_back(static_cast<char>(mem_[lin(buf_seg, buf_off + 1 + i)]));
    }
    return s;
}

std::pair<int, int> Machine::current_quadrant() const {
    if (state_ != State::Running) return {1, 1};
    auto read16 = [this](std::uint16_t seg, std::uint16_t off) -> std::uint16_t {
        const std::uint32_t a = lin(seg, off);
        return static_cast<std::uint16_t>(mem_[a] | (mem_[(a + 1) & 0xFFFFF] << 8));
    };
    return {read16(cpu.s[DS], 0xd33), read16(cpu.s[DS], 0xd35)};
}

std::pair<int, int> Machine::current_sector() const {
    if (state_ != State::Running) return {1, 1};
    auto read16 = [this](std::uint16_t seg, std::uint16_t off) -> std::uint16_t {
        const std::uint32_t a = lin(seg, off);
        return static_cast<std::uint16_t>(mem_[a] | (mem_[(a + 1) & 0xFFFFF] << 8));
    };
    return {read16(cpu.s[DS], 0xd37), read16(cpu.s[DS], 0xd39)};
}

void Machine::type_string(std::string_view s) {
    for (char ch : s) {
        if (ch == '\r' || ch == '\n') {
            key_down(0x1C, 0x0D);
        } else if (ch == '\x1B') {
            key_down(0x01, 0x1B);
        } else if (auto k = key_for_char(ch)) {
            key_down(k->scan, k->ascii);
        }
    }
}

void Machine::trigger_save() {
    type_string("\x1bSAVE\r");
}

bool Machine::handle_restore() {
    unsaved_progress_ = false;
    if (restore_hook_) {
        const bool proceed = restore_hook_(*this);
        if (!proceed) {
            wr8(cpu.s[DS], 0x0E66, 'N');
            far_return(0);
            return true;
        }
    } else {
        const auto p = host_path("TREK.SAV", false);
        if (!p || !std::filesystem::exists(*p)) {
            wr8(cpu.s[DS], 0x0E66, 'N');
            far_return(0);
            return true;
        }
    }
    return false;
}

void Machine::handle_save() {
    unsaved_progress_ = false;
    auto p = host_path("TREK.SAV", true);
    if (p) {
        if (!active_save_path_) active_save_path_ = *p;
        if (save_hook_) save_hook_(*this, *p);
    }
}

void Machine::handle_cmd_input() {
    const std::uint8_t len = mem_[lin(cpu.s[DS], 0x1065)];
    if (len > 0) {
        std::string cmd;
        for (std::uint8_t i = 0; i < len; ++i) {
            cmd.push_back(static_cast<char>(mem_[lin(cpu.s[DS], 0x1066 + i)]));
        }
        if (cmd != "SAVE" && cmd != "save") {
            unsaved_progress_ = true;
        }

        // If command is M/m with coordinates (e.g. "M 3,5", "M 6,2,3,5", "M 6,2,"):
        // Forward the coordinates to the Move routine's "Quad, Sector: " prompt.
        std::string_view sv = cmd;
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
            sv.remove_prefix(1);
        }
        if (!sv.empty() && (sv.front() == 'M' || sv.front() == 'm')) {
            sv.remove_prefix(1);
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
                sv.remove_prefix(1);
            }
            if (!sv.empty() && (std::isdigit(static_cast<unsigned char>(sv.front())) || sv.front() == ',')) {
                bool ends_with_comma = (sv.back() == ',');
                std::string coords(sv);
                if (!ends_with_comma) {
                    coords.push_back('\r');
                }
                type_string(coords);
            }
        }
    }
}

bool Machine::has_unsaved_progress() const {
    if (state_ != State::Running) return false;
    if (mem_[lin(cpu.s[DS], 0x1526)] == 1) return false;
    return unsaved_progress_;
}

std::string Machine::current_command() const {
    if (state_ != State::Running) return "";
    const std::uint8_t len = mem_[lin(cpu.s[DS], 0x1065)];
    std::string cmd;
    cmd.reserve(len);
    for (std::uint8_t i = 0; i < len; ++i) {
        cmd.push_back(static_cast<char>(mem_[lin(cpu.s[DS], 0x1066 + i)]));
    }
    return cmd;
}

}  // namespace trek::dos

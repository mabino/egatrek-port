#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "dos/cpu.h"
#include "dos/ega.h"
#include "dos/speaker.h"

namespace trek::dos {

class Machine;

// The recompiled program, produced by `trekre recomp` from the user's TREK.EXE.
struct Program {
    struct Segment {
        std::uint32_t begin, end;      // linear range of recompiled blocks
        void (*run)(Machine&);         // runs blocks until control leaves the segment
    };
    std::span<const std::uint8_t> image;  // relocated load image
    std::uint16_t load_segment = 0;
    std::uint16_t cs = 0, ip = 0, ss = 0, sp = 0;
    std::uint16_t min_alloc = 0;          // paragraphs needed beyond the image
    std::span<const std::uint32_t> blocks;  // sorted linear addresses of recompiled blocks
    std::span<const Segment> segments;
    std::span<const std::uint8_t> code_bits;  // 1 bit per image byte that belongs to recompiled code
    std::span<const std::uint8_t> code_image; // the image as the recompiler decoded it (after run-time moves)
};

// Host side of the DOS file system: where files the game opens come from.
struct FileSystem {
    std::filesystem::path data_dir;   // read-only originals (TREK.EXE folder)
    std::filesystem::path save_dir;   // TREK.SAV and anything else the game writes
};

// A real-mode PC running one recompiled DOS program: 1 MB of memory, an EGA,
// a PC speaker, BIOS and DOS services implemented natively (high-level
// emulation), and a dispatcher that runs recompiled blocks or, for code the
// recompiler did not find, an interpreter.
class Machine {
public:
    static constexpr std::uint32_t kMemSize = 0x100000;
    static constexpr std::uint16_t kBiosSeg = 0xF000;  // interrupt stubs and ROM tables live here
    static constexpr std::uint16_t kStubBase = 0x0100;   // F000:0100 + 4*n is the stub for INT n

    Machine(const Program& program, FileSystem fs);
    ~Machine();

    // Runs until the program yields (waiting for input or time) or `budget`
    // blocks have executed. `now_ms` is host time for the BIOS clock.
    enum class State { Running, Exited, Faulted };
    State run(std::int64_t budget, double now_ms);
    State state() const { return state_; }
    int exit_code() const { return exit_code_; }
    const std::string& fault() const { return fault_; }

    // Input from the host: BIOS scan code and ASCII (0 for extended keys).
    void key_down(std::uint8_t scan, std::uint8_t ascii);
    void set_shift_state(std::uint8_t bios_flags) { shift_flags_ = bios_flags; }
    bool waiting_for_key() const { return waiting_key_; }

    Ega& ega() { return ega_; }
    Speaker& speaker() { return speaker_; }

    // For deterministic tests: fixed DOS date/time and no host clock.
    void set_clock_override(std::optional<double> ms) { clock_override_ = ms; }

    // Statistics for development: code the recompiler missed.
    const std::map<std::uint32_t, std::uint64_t>& interpreted() const { return interpreted_; }
    // CS last seen when interpretation started at each address.
    std::uint16_t interpreted_cs(std::uint32_t pc) const {
        auto it = interpreted_cs_.find(pc);
        return it == interpreted_cs_.end() ? 0 : it->second;
    }
    std::uint64_t blocks_run() const { return blocks_run_; }
    // Count of DOS file writes; hosts that must persist saves (the browser) watch it.
    std::uint64_t file_writes() const { return file_writes_; }
    // Whether the program has written to this linear address inside its own
    // image (code built at run time is not a valid recompiler seed).
    // Development: log the first write by each instruction into [lo, hi).
    void set_write_watch(std::uint32_t lo, std::uint32_t hi) { watch_lo_ = lo, watch_hi_ = hi; }
    bool image_written(std::uint32_t a) const {
        return a - image_lo_ < image_size_ && written_[a - image_lo_];
    }

    // Development tracing: the last kTraceSize block (or interpreted
    // instruction) addresses, oldest first.
    static constexpr std::size_t kTraceSize = 256;
    void set_tracing(bool on) { tracing = on; }
    // Called with the address of every block about to run (recompiled or
    // interpreted) while tracing; returning false stops the machine before
    // the block executes. Used by the lockstep checker.
    std::function<bool(std::uint32_t)> block_hook;
    // Run everything through the interpreter (reference for lockstep checks).
    void set_interpret_only(bool on) { interpret_only_ = on; }
    // BIOS ticks advance once per 64 reads of 0040:006C instead of with host
    // time, so two runs see identical clocks.
    void set_deterministic_ticks(bool on) { deterministic_ticks_ = on; }
    std::vector<std::uint32_t> trace_tail() const;

    // ---- Used by recompiled code and the interpreter ----
    Cpu cpu;
    bool tracing = false;
    bool log_keys = false;  // development: log every key the program reads
    bool trace(std::uint32_t pc) {
        if (block_hook && !(pc & 0x80000000u) && !block_hook(pc)) {
            budget = 0;
            return false;
        }
        trace_[trace_pos_++ % kTraceSize] = pc;
        return true;
    }
    std::int64_t budget = 0;  // blocks left in this slice; <= 0 makes recompiled code return

    std::uint8_t* mem() { return mem_.data(); }
    std::uint8_t rd8(std::uint16_t seg, std::uint16_t off) { return rd8l(lin(seg, off)); }
    std::uint16_t rd16(std::uint16_t seg, std::uint16_t off) {
        return static_cast<std::uint16_t>(rd8(seg, off) | rd8(seg, static_cast<std::uint16_t>(off + 1)) << 8);
    }
    void wr8(std::uint16_t seg, std::uint16_t off, std::uint8_t v) { wr8l(lin(seg, off), v); }
    void wr16(std::uint16_t seg, std::uint16_t off, std::uint16_t v) {
        wr8(seg, off, static_cast<std::uint8_t>(v));
        wr8(seg, static_cast<std::uint16_t>(off + 1), static_cast<std::uint8_t>(v >> 8));
    }
    static std::uint32_t lin(std::uint16_t seg, std::uint16_t off) { return (seg * 16u + off) & 0xFFFFF; }
    std::uint8_t rd8l(std::uint32_t a) {
        if (a >= 0xA0000 && a < 0xC0000) return video_read(a);
        if (a == 0x46C) read_ticks();
        return mem_[a];
    }
    void wr8l(std::uint32_t a, std::uint8_t v) {
        if (a >= 0xA0000) {
            if (a < 0xC0000) video_write(a, v);
            return;  // ROM
        }
        if (a - image_lo_ < image_size_) {
            if (code_byte(a - image_lo_) && program_.code_image[a - image_lo_] != v) self_modified(a);
            written_[a - image_lo_] = true;
            if (a >= watch_lo_ && a < watch_hi_) watched_write(a, v);
        }
        mem_[a] = v;
    }

    void push(std::uint16_t v) {
        cpu.r[SP] = static_cast<std::uint16_t>(cpu.r[SP] - 2);
        wr16(cpu.s[SS], cpu.r[SP], v);
    }
    std::uint16_t pop() {
        const std::uint16_t v = rd16(cpu.s[SS], cpu.r[SP]);
        cpu.r[SP] = static_cast<std::uint16_t>(cpu.r[SP] + 2);
        return v;
    }
    // Control transfers that leave straight-line code. They set cpu.pc.
    void near_jump(std::uint16_t ip) { cpu.pc = lin(cpu.s[CS], ip); }
    void far_jump(std::uint16_t cs, std::uint16_t ip) {
        cpu.s[CS] = cs;
        cpu.pc = lin(cs, ip);
    }
    void near_return(std::uint16_t extra) {
        const std::uint16_t ip = pop();
        cpu.r[SP] = static_cast<std::uint16_t>(cpu.r[SP] + extra);
        near_jump(ip);
    }
    void far_return(std::uint16_t extra) {
        const std::uint16_t ip = pop();
        const std::uint16_t cs = pop();
        cpu.r[SP] = static_cast<std::uint16_t>(cpu.r[SP] + extra);
        far_jump(cs, ip);
    }
    void iret() {
        const std::uint16_t ip = pop();
        const std::uint16_t cs = pop();
        cpu.unpack(pop());
        far_jump(cs, ip);
    }
    // Raise interrupt n with the return address `ret_pc` (linear, in the current CS).
    void interrupt(std::uint8_t n, std::uint32_t ret_pc);

    std::uint8_t in8(std::uint16_t port);
    std::uint16_t in16(std::uint16_t port) {
        return static_cast<std::uint16_t>(in8(port) | in8(static_cast<std::uint16_t>(port + 1)) << 8);
    }
    void out8(std::uint16_t port, std::uint8_t v);
    void out16(std::uint16_t port, std::uint16_t v) {
        out8(port, static_cast<std::uint8_t>(v));
        out8(static_cast<std::uint16_t>(port + 1), static_cast<std::uint8_t>(v >> 8));
    }
    void halt() { budget = 0; }  // HLT: yield until the next slice

    // Executes one instruction at cpu.pc (interpreter.cpp). Returns false on a decode failure.
    bool step();

private:
    friend class Bios;
    void reset();
    bool is_block(std::uint32_t pc) const;
    const Program::Segment* segment_for(std::uint32_t pc) const;
    bool run_stub(int n);  // false: blocked, retry later
    void sync_ticks();
    void read_ticks();  // a program read of the tick counter
    void deliver_timer();
    std::uint8_t video_read(std::uint32_t a);
    void video_write(std::uint32_t a, std::uint8_t v);
    void fail(std::string why);
    bool code_byte(std::uint32_t i) const {
        return i / 8 < program_.code_bits.size() && (program_.code_bits[i / 8] >> (i % 8) & 1);
    }
    void self_modified(std::uint32_t a);
    std::uint32_t image_lo_ = 0, image_size_ = 0;
    std::vector<bool> written_;
    std::uint32_t watch_lo_ = 0, watch_hi_ = 0;
    void watched_write(std::uint32_t a, std::uint8_t v);

    // BIOS and DOS services (bios.cpp, dos.cpp).
    bool int10();
    bool int16();
    bool int21();
    bool int1a();
    void int21_files(std::uint8_t ah);
    std::optional<std::filesystem::path> host_path(const std::string& dos_name, bool for_write) const;
    std::string read_asciiz(std::uint16_t seg, std::uint16_t off) const;

    const Program& program_;
    FileSystem fs_;
    std::vector<std::uint8_t> mem_;
    Ega ega_;
    Speaker speaker_;
    State state_ = State::Running;
    int exit_code_ = 0;
    std::string fault_;

    std::deque<std::uint16_t> keys_;
    std::uint8_t shift_flags_ = 0;
    bool waiting_key_ = false;

    double now_ms_ = 0, start_ms_ = -1;
    std::optional<double> clock_override_;
    std::uint32_t ticks_ = 0, delivered_ticks_ = 0;

    std::uint8_t pit_mode_ = 0, pit_latch_lo_ = 0;
    bool pit_lo_written_ = false;
    std::uint16_t pit2_divisor_ = 0;
    std::uint8_t port61_ = 0;
    std::uint16_t pit_counter_ = 0xFFFF;
    bool refresh_ = false;

    std::map<int, std::unique_ptr<struct DosFile>> files_;
    std::map<std::uint32_t, std::uint64_t> interpreted_;
    std::map<std::uint32_t, std::uint16_t> interpreted_cs_;
    std::array<std::uint32_t, kTraceSize> trace_{};
    bool interpret_only_ = false;
    bool deterministic_ticks_ = false;
    std::uint64_t tick_reads_ = 0;
    std::size_t trace_pos_ = 0;
    std::uint64_t blocks_run_ = 0;
    std::uint64_t file_writes_ = 0;
    std::set<int> warned_;
};

}  // namespace trek::dos

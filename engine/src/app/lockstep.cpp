// Lockstep checker: runs the recompiled program and the interpreter side by
// side from the same start, with deterministic clocks and the same keys, and
// stops at the first block where their CPU state differs.
//   trek_lockstep [--blocks N] [--keys SCRIPT]
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <string>

#include "core/data_path.h"
#include "dos/keymap.h"
#include "dos/machine.h"
#include "dos/recomp.h"

namespace fs = std::filesystem;
using trek::dos::Cpu;
using trek::dos::Machine;

namespace {
struct Snap {
    std::uint32_t pc;
    Cpu cpu;
};

bool same(const Cpu& a, const Cpu& b) {
    return std::memcmp(a.r, b.r, sizeof a.r) == 0 && std::memcmp(a.s, b.s, sizeof a.s) == 0 &&
           (a.pack() & 0x0FD5) == (b.pack() & 0x0FD5);  // CF PF AF ZF SF TF IF DF OF
}

void print(const char* who, const Snap& s) {
    const Cpu& c = s.cpu;
    std::printf("%-6s %05X  AX=%04X BX=%04X CX=%04X DX=%04X SI=%04X DI=%04X BP=%04X SP=%04X  "
                "ES=%04X CS=%04X SS=%04X DS=%04X  F=%04X\n",
                who, s.pc, c.r[0], c.r[3], c.r[1], c.r[2], c.r[6], c.r[7], c.r[5], c.r[4], c.s[0], c.s[1], c.s[2],
                c.s[3], c.pack());
}
}  // namespace

int main(int argc, char** argv) {
    long long limit = 200'000'000;
    double max_seconds = 120;
    std::string keys;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--blocks")) limit = std::atoll(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--keys")) keys = argv[i + 1];
        else if (!std::strcmp(argv[i], "--seconds")) max_seconds = std::atof(argv[i + 1]);
    }
    auto data = trek::DataPath::from_environment();
    const fs::path save = fs::temp_directory_path() / "egatrek-lockstep";
    fs::create_directories(save);
    trek::dos::FileSystem files{data ? data->root() : fs::path(), save};
    Machine rec(trek::recomp::program(), files), ref(trek::recomp::program(), files);
    for (Machine* m : {&rec, &ref}) {
        m->set_deterministic_ticks(true);
        m->set_clock_override(0.0);
        m->set_tracing(true);
    }
    ref.set_interpret_only(true);

    std::deque<Snap> pending;  // recorded by the recompiled run, awaiting the reference
    std::deque<Snap> history;  // last matched blocks, for context
    long long matched = 0, recorded = 0;
    bool diverged = false;
    // Keys reach the reference at the same block index as the recompiled run.
    std::deque<std::pair<long long, trek::dos::BiosKey>> keyq;
    rec.block_hook = [&](std::uint32_t pc) {
        pending.push_back({pc, rec.cpu});
        ++recorded;
        return true;
    };
    ref.block_hook = [&](std::uint32_t pc) {
        if (diverged || pending.empty()) return false;  // caught up: stop before this block
        while (!keyq.empty() && keyq.front().first == matched) {
            ref.key_down(keyq.front().second.scan, keyq.front().second.ascii);
            keyq.pop_front();
        }
        const Snap want = pending.front();
        pending.pop_front();
        const Snap got{pc, ref.cpu};
        if (want.pc != pc || !same(want.cpu, got.cpu)) {
            diverged = true;
            ref.budget = 0;
            std::printf("DIVERGED after %lld matching blocks\n", matched);
            for (const auto& h : history) print("ok", h);
            print("recomp", want);
            print("interp", got);
            return false;
        }
        ++matched;
        history.push_back(got);
        if (history.size() > 12) history.pop_front();
        return true;
    };

    const auto script = trek::dos::parse_script(keys);
    std::size_t next_key = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (matched < limit && !diverged) {
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > max_seconds) {
            std::printf("time limit: recompiled run at %05X, %zu of %zu keys used\n", rec.cpu.pc, next_key,
                        script.size());
            break;
        }
        rec.run(2000, 0.0);
        while (!pending.empty() && !diverged && ref.state() == Machine::State::Running) {
            // A reference blocked in INT 16h reaches no block boundary: hand keys over here too.
            while (!keyq.empty() && keyq.front().first <= matched) {
                ref.key_down(keyq.front().second.scan, keyq.front().second.ascii);
                keyq.pop_front();
            }
            const std::size_t before = pending.size();
            ref.run(1'000'000'000, 0.0);
            if (pending.size() == before && !diverged) break;  // reference is blocked (waiting for a key)
        }
        if (rec.state() != Machine::State::Running || ref.state() != Machine::State::Running) {
            std::printf("stopped: recomp %s, interp %s\n", rec.fault().c_str(), ref.fault().c_str());
            break;
        }
        if (rec.waiting_for_key() && pending.empty()) {
            if (next_key >= script.size()) {
                std::printf("program waits for a key; script exhausted\n");
                break;
            }
            const auto k = script[next_key++];
            if (k.scan || k.ascii) {
                rec.key_down(k.scan, k.ascii);
                keyq.push_back({recorded, k});
            }
        }
    }
    std::printf("%s after %lld blocks\n", diverged ? "diverged" : "in lockstep", matched);
    return diverged ? 1 : 0;
}

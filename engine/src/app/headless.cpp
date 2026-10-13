// Development runner: plays the recompiled game without a window.
//   trek_headless [--keys SCRIPT] [--frames N] [--shot FILE.bmp] [--shots-dir DIR --every N]
// Keys are fed one per frame whenever the game waits for input. Time is
// simulated (60 frames per second), so runs are repeatable.
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "core/data_path.h"
#include "dos/keymap.h"
#include "dos/machine.h"
#include "dos/recomp.h"
#include "dos/rom_font.h"

namespace fs = std::filesystem;

static bool save_bmp(trek::dos::Machine& m, const std::string& path) {
    std::vector<std::uint32_t> argb(trek::dos::Ega::kWidth * trek::dos::Ega::kHeight);
    m.ega().render(argb, m.mem() + 0xB8000, trek::dos::kRomFont8x8, true);
    SDL_Surface* s = SDL_CreateSurfaceFrom(trek::dos::Ega::kWidth, trek::dos::Ega::kHeight, SDL_PIXELFORMAT_ARGB8888,
                                           argb.data(), trek::dos::Ega::kWidth * 4);
    const bool ok = s && SDL_SaveBMP(s, path.c_str());
    SDL_DestroySurface(s);
    return ok;
}

int main(int argc, char** argv) {
    std::string keys, shot, shots_dir, save_dir = (fs::temp_directory_path() / "egatrek-headless").string();
    long frames = 600, every = 0, budget = 400000;
    bool trace = false, verbose = false;
    std::string dump, watch;
    for (int i = 1; i < argc; ++i) {
        auto arg = [&](const char* name) { return std::strcmp(argv[i], name) == 0 && i + 1 < argc; };
        if (arg("--keys")) keys = argv[++i];
        else if (arg("--frames")) frames = std::atol(argv[++i]);
        else if (arg("--shot")) shot = argv[++i];
        else if (arg("--shots-dir")) shots_dir = argv[++i];
        else if (arg("--every")) every = std::atol(argv[++i]);
        else if (arg("--budget")) budget = std::atol(argv[++i]);
        else if (arg("--save-dir")) save_dir = argv[++i];
        else if (std::strcmp(argv[i], "--trace") == 0) trace = true;
        else if (std::strcmp(argv[i], "--verbose") == 0) verbose = true;
        else if (arg("--dump-interpreted")) dump = argv[++i];
        else if (arg("--watch")) watch = argv[++i];
        else {
            std::fprintf(stderr, "unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    auto data = trek::DataPath::from_environment();
    fs::create_directories(save_dir);
    trek::dos::Machine m(trek::recomp::program(), {data ? data->root() : fs::path(), save_dir});
    m.set_tracing(trace);
    m.log_keys = verbose;
    if (!watch.empty()) {
        const auto dash = watch.find('-');
        m.set_write_watch(std::stoul(watch.substr(0, dash), nullptr, 16), std::stoul(watch.substr(dash + 1), nullptr, 16));
    }
    const auto script = trek::dos::parse_script(keys);
    std::size_t next_key = 0;
    int idle = 0;
    double now = 0;
    for (long f = 0; f < frames; ++f) {
        m.set_clock_override(now);
        const auto state = m.run(budget, now);
        now += 1000.0 / 60.0;
        if (state != trek::dos::Machine::State::Running) {
            std::printf("frame %ld: %s %s (exit code %d)\n", f,
                        state == trek::dos::Machine::State::Exited ? "exited" : "FAULT", m.fault().c_str(),
                        m.exit_code());
            break;
        }
        // {WAIT n} entries pass one per frame. A key goes in once the game has
        // been waiting for input for a few frames, as a person would type:
        // keys sent earlier can be lost to the game flushing the keyboard.
        ++idle;
        if (next_key < script.size()) {
            const auto k = script[next_key];
            if (!k.scan && !k.ascii) {
                ++next_key;
            } else if (idle >= 6 && m.waiting_for_key()) {
                if (verbose) std::printf("frame %ld: key %02X/%02X\n", f, k.scan, k.ascii);
                m.key_down(k.scan, k.ascii);
                ++next_key;
                idle = 0;
            }
        }
        if (every > 0 && !shots_dir.empty() && f % every == 0)
            save_bmp(m, (fs::path(shots_dir) / ("frame" + std::to_string(f) + ".bmp")).string());
    }
    if (!shot.empty()) save_bmp(m, shot);
    std::printf("blocks run: %llu, keys used: %zu/%zu, video mode %02Xh, pc %05X\n",
                static_cast<unsigned long long>(m.blocks_run()), next_key, script.size(), m.ega().mode(), m.cpu.pc);
    if (trace) {
        std::printf("last blocks (* = interpreted):");
        for (auto pc : m.trace_tail())
            std::printf(" %05X%s", pc & 0xFFFFF, pc & 0x80000000u ? "*" : "");
        std::printf("\n");
    }
    if (!dump.empty()) {
        // seg:off relative to the load segment, as trekre's --extra-seeds expects.
        if (std::FILE* f = std::fopen(dump.c_str(), "a")) {
            const auto& prog = trek::recomp::program();
            const std::uint32_t base = prog.load_segment * 16u;
            for (const auto& [pc, n] : m.interpreted()) {
                // Code the program wrote itself is no seed, unless it matches what the
                // recompiler decodes there (code moved into place, see hints.MOVED).
                if (m.image_written(pc) && m.mem()[pc] != prog.code_image[pc - base]) continue;
                // CS:IP with CS relative to the load segment: decoding needs the real CS.
                const std::uint16_t cs = m.interpreted_cs(pc);
                std::fprintf(f, "%04X:%04X\n", cs - prog.load_segment, static_cast<unsigned>(pc - cs * 16u));
            }
            std::fclose(f);
        }
    }
    std::size_t shown = 0;
    for (const auto& [pc, n] : m.interpreted()) {
        if (shown++ < 40)
            std::printf("interpreted entry %05X x%llu%s\n", pc, static_cast<unsigned long long>(n),
                        m.image_written(pc) ? " (written at run time)" : "");
    }
    return m.state() == trek::dos::Machine::State::Faulted ? 1 : 0;
}

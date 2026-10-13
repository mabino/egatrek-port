// DOS services (INT 21h): the subset Turbo Pascal 4's runtime and the game use.
#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <system_error>

#include "dos/dos_file.h"
#include "dos/machine.h"

namespace trek::dos {

namespace fs = std::filesystem;

namespace {
constexpr int kFirstFileHandle = 5;
constexpr double kTicksPerMsDos = 1193182.0 / 65536.0 / 1000.0;

std::string upper(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

// DOS error codes.
constexpr std::uint16_t kFileNotFound = 2, kTooManyFiles = 4, kAccessDenied = 5, kBadHandle = 6;
}  // namespace

std::string Machine::read_asciiz(std::uint16_t seg, std::uint16_t off) const {
    std::string s;
    for (int i = 0; i < 128; ++i) {
        const std::uint8_t ch = mem_[lin(seg, static_cast<std::uint16_t>(off + i))];
        if (!ch) break;
        s.push_back(static_cast<char>(ch));
    }
    return s;
}

std::optional<fs::path> Machine::host_path(const std::string& dos_name, bool for_write) const {
    // The game only uses bare names in the current directory; drop any drive or path.
    std::string name = dos_name;
    if (auto cut = name.find_last_of(":\\/"); cut != std::string::npos) name = name.substr(cut + 1);
    name = upper(name);
    if (name.empty() || name == "." || name == "..") return std::nullopt;
    const auto find_in = [&](const fs::path& dir) -> std::optional<fs::path> {
        std::error_code ec;
        if (dir.empty()) return std::nullopt;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (upper(e.path().filename().string()) == name) return e.path();
        return std::nullopt;
    };
    if (for_write) {
        if (auto hit = find_in(fs_.save_dir)) return hit;
        return fs_.save_dir / name;
    }
    if (auto hit = find_in(fs_.save_dir)) return hit;
    return find_in(fs_.data_dir);
}

void Machine::int21_files(std::uint8_t ah) {
    auto error = [this](std::uint16_t code) {
        cpu.cf = true;
        cpu.r[AX] = code;
    };
    auto ok = [this] { cpu.cf = false; };
    auto handle = [this]() -> DosFile* {
        auto it = files_.find(cpu.r[BX]);
        return it == files_.end() ? nullptr : it->second.get();
    };
    auto new_handle = [this]() -> int {
        for (int h = kFirstFileHandle; h < 20; ++h)
            if (!files_.count(h)) return h;
        return -1;
    };

    switch (ah) {
        case 0x3C:  // create
        case 0x3D: {  // open
            const std::string name = read_asciiz(cpu.s[DS], cpu.r[DX]);
            const int mode = cpu.get8(0) & 3;
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "DOS %s \"%s\" mode %d", ah == 0x3C ? "create" : "open", name.c_str(), mode);
            const bool create = ah == 0x3C;
            const auto path = host_path(name, create || mode != 0);
            if (!path) return error(kFileNotFound);
            std::error_code ec;
            if (!create && !fs::exists(*path, ec)) return error(kFileNotFound);
            if (create) fs::create_directories(path->parent_path(), ec);
            const int h = new_handle();
            if (h < 0) return error(kTooManyFiles);
            auto f = std::make_unique<DosFile>();
            f->path = *path;
            f->fp = std::fopen(path->string().c_str(), create ? "w+b" : mode == 0 ? "rb" : "r+b");
            if (!f->fp && !create && mode != 0) {
                // A read/write open of an original file: copy it to the save folder first.
                if (auto orig = host_path(name, false); orig && *orig != *path) {
                    fs::copy_file(*orig, *path, fs::copy_options::overwrite_existing, ec);
                    f->fp = std::fopen(path->string().c_str(), "r+b");
                }
            }
            if (!f->fp) return error(create ? kAccessDenied : kFileNotFound);
            files_[h] = std::move(f);
            cpu.r[AX] = static_cast<std::uint16_t>(h);
            return ok();
        }
        case 0x3E:  // close
            if (cpu.r[BX] < kFirstFileHandle) return ok();
            if (!files_.erase(cpu.r[BX])) return error(kBadHandle);
            ++file_writes_;  // closing flushes; let the host persist it
            return ok();
        case 0x3F: {  // read
            if (cpu.r[BX] == 0) {  // console input: the game reads keys through Crt instead
                cpu.r[AX] = 0;
                return ok();
            }
            DosFile* f = handle();
            if (!f) return error(kBadHandle);
            std::uint16_t n = 0;
            for (; n < cpu.r[CX]; ++n) {
                const int ch = std::fgetc(f->fp);
                if (ch == EOF) break;
                wr8(cpu.s[DS], static_cast<std::uint16_t>(cpu.r[DX] + n), static_cast<std::uint8_t>(ch));
            }
            cpu.r[AX] = n;
            return ok();
        }
        case 0x40: {  // write
            if (cpu.r[BX] == 1 || cpu.r[BX] == 2) {
                for (std::uint16_t i = 0; i < cpu.r[CX]; ++i) {
                    const std::uint16_t ax = cpu.r[AX];
                    cpu.r[AX] = static_cast<std::uint16_t>(0x0E00 | rd8(cpu.s[DS], static_cast<std::uint16_t>(cpu.r[DX] + i)));
                    int10();
                    cpu.r[AX] = ax;
                }
                cpu.r[AX] = cpu.r[CX];
                return ok();
            }
            DosFile* f = handle();
            if (!f) return error(kBadHandle);
            if (cpu.r[CX] == 0) {  // truncate at the current position
                std::fflush(f->fp);
                std::error_code ec;
                fs::resize_file(f->path, static_cast<std::uintmax_t>(std::ftell(f->fp)), ec);
                cpu.r[AX] = 0;
                return ok();
            }
            std::uint16_t n = 0;
            for (; n < cpu.r[CX]; ++n)
                if (std::fputc(rd8(cpu.s[DS], static_cast<std::uint16_t>(cpu.r[DX] + n)), f->fp) == EOF) break;
            std::fflush(f->fp);
            cpu.r[AX] = n;
            return ok();
        }
        case 0x41: {  // delete
            const auto path = host_path(read_asciiz(cpu.s[DS], cpu.r[DX]), true);
            std::error_code ec;
            if (!path || !fs::remove(*path, ec)) return error(kFileNotFound);
            return ok();
        }
        case 0x42: {  // seek
            DosFile* f = handle();
            if (!f) return error(kBadHandle);
            const long off = static_cast<long>(static_cast<std::int32_t>(cpu.r[CX] << 16 | cpu.r[DX]));
            const int whence = cpu.get8(0) == 0 ? SEEK_SET : cpu.get8(0) == 1 ? SEEK_CUR : SEEK_END;
            if (std::fseek(f->fp, off, whence) != 0) return error(0x19);
            const long pos = std::ftell(f->fp);
            cpu.r[AX] = static_cast<std::uint16_t>(pos);
            cpu.r[DX] = static_cast<std::uint16_t>(pos >> 16);
            return ok();
        }
        case 0x43: {  // get/set attributes
            const auto path = host_path(read_asciiz(cpu.s[DS], cpu.r[DX]), false);
            if (!path) return error(kFileNotFound);
            cpu.r[CX] = 0x20;
            return ok();
        }
        case 0x44:  // IOCTL: device information
            if (cpu.get8(0) == 0x00) {
                if (cpu.r[BX] < kFirstFileHandle) cpu.r[DX] = cpu.r[BX] == 0 ? 0x80D1 : 0x80D2;
                else if (handle()) cpu.r[DX] = 0x0002;  // disk file on drive C:
                else return error(kBadHandle);
                return ok();
            }
            return error(1);
        default: return error(1);
    }
}

bool Machine::int21() {
    const std::uint8_t ah = cpu.get8(4);
    switch (ah) {
        case 0x02: {
            const std::uint16_t ax = cpu.r[AX];
            cpu.r[AX] = static_cast<std::uint16_t>(0x0E00 | cpu.get8(2));
            int10();
            cpu.r[AX] = ax;
            break;
        }
        case 0x06:
            if (cpu.get8(2) == 0xFF) {
                cpu.zf = true;
                cpu.set8(0, 0);
            }
            break;
        case 0x0B: cpu.set8(0, 0); break;  // no console input pending
        case 0x0E: cpu.set8(0, 26); break;
        case 0x19: cpu.set8(0, 2); break;  // current drive C:
        case 0x1A: wr16(0x40, 0xF0, cpu.r[DX]), wr16(0x40, 0xF2, cpu.s[DS]); break;  // DTA in the BDA's ICA
        case 0x25:
            wr16(0, static_cast<std::uint16_t>(cpu.get8(0) * 4), cpu.r[DX]);
            wr16(0, static_cast<std::uint16_t>(cpu.get8(0) * 4 + 2), cpu.s[DS]);
            break;
        case 0x2A:
        case 0x2C: {
            std::tm lt{};
            if (clock_override_ || deterministic_ticks_) {
                lt.tm_year = 88, lt.tm_mon = 4, lt.tm_mday = 9;  // 1988-05-09, the release date
                if (deterministic_ticks_) read_ticks();  // reading the clock advances it
                const double ms = deterministic_ticks_ ? ticks_ / kTicksPerMsDos : now_ms_ - start_ms_;
                lt.tm_hour = 12 + static_cast<int>(ms / 3600000) % 12;
                lt.tm_min = static_cast<int>(ms / 60000) % 60;
                lt.tm_sec = static_cast<int>(ms / 1000) % 60;
            } else {
                const std::time_t t = std::time(nullptr);
#if defined(_WIN32)
                localtime_s(&lt, &t);
#else
                localtime_r(&t, &lt);
#endif
            }
            if (ah == 0x2A) {
                cpu.r[CX] = static_cast<std::uint16_t>(lt.tm_year + 1900);
                cpu.set8(6, static_cast<std::uint8_t>(lt.tm_mon + 1));
                cpu.set8(2, static_cast<std::uint8_t>(lt.tm_mday));
                cpu.set8(0, static_cast<std::uint8_t>(lt.tm_wday));
            } else {
                // Hundredths come from the emulated clock so Randomize differs between runs.
                const double ms = deterministic_ticks_ ? ticks_ / kTicksPerMsDos : now_ms_ - start_ms_;
                const int hundredths = static_cast<int>(ms / 10) % 100;
                cpu.set8(5, static_cast<std::uint8_t>(lt.tm_hour));
                cpu.set8(1, static_cast<std::uint8_t>(lt.tm_min));
                cpu.set8(6, static_cast<std::uint8_t>(lt.tm_sec));
                cpu.set8(2, static_cast<std::uint8_t>(hundredths));
            }
            break;
        }
        case 0x2F:
            cpu.s[ES] = rd16(0x40, 0xF2);
            cpu.r[BX] = rd16(0x40, 0xF0);
            break;
        case 0x30: cpu.r[AX] = 0x1E03; cpu.r[BX] = cpu.r[CX] = 0; break;  // DOS 3.30
        case 0x33: cpu.set8(2, 0); break;  // Ctrl-Break checking off
        case 0x35:
            cpu.r[BX] = rd16(0, static_cast<std::uint16_t>(cpu.get8(0) * 4));
            cpu.s[ES] = rd16(0, static_cast<std::uint16_t>(cpu.get8(0) * 4 + 2));
            break;
        case 0x3B: cpu.cf = false; break;
        case 0x47: wr8(cpu.s[DS], cpu.r[SI], 0); cpu.cf = false; break;  // current directory: root
        case 0x48: cpu.cf = true; cpu.r[AX] = 8; cpu.r[BX] = 0; break;  // no DOS memory to spare
        case 0x49: cpu.cf = false; break;
        case 0x4A: cpu.cf = false; break;  // resize: always fits (we own up to A000h)
        case 0x4C:
            exit_code_ = cpu.get8(0);
            state_ = State::Exited;
            budget = 0;
            break;
        case 0x4E: cpu.cf = true; cpu.r[AX] = 18; break;  // find first: nothing
        case 0x4F: cpu.cf = true; cpu.r[AX] = 18; break;
        default:
            if (ah >= 0x3C && ah <= 0x44) {
                int21_files(ah);
                break;
            }
            if (warned_.insert(0x21000 + ah).second) SDL_Log("unhandled INT 21h AH=%02Xh", ah);
            cpu.cf = true;
            cpu.r[AX] = 1;
            break;
    }
    return true;
}

}  // namespace trek::dos

// BIOS services: video (INT 10h), keyboard (INT 16h) and clock (INT 1Ah).
#include <SDL3/SDL_log.h>

#include <algorithm>
#include <utility>

#include "dos/machine.h"

namespace trek::dos {

namespace {
constexpr std::uint16_t kFontOff = 0xFA6E;
constexpr std::uint16_t kFontHighOff = 0xF000;
}  // namespace

bool Machine::int10() {
    const std::uint8_t ah = cpu.get8(4), al = cpu.get8(0);
    auto cursor = [this] { return std::pair<int, int>(mem_[0x451], mem_[0x450]); };  // row, col
    auto set_cursor = [this](int row, int col) {
        mem_[0x451] = static_cast<std::uint8_t>(row);
        mem_[0x450] = static_cast<std::uint8_t>(col);
        ega_.port_out(0x3D4, 0x0E);
        ega_.port_out(0x3D5, static_cast<std::uint8_t>((row * 80 + col) >> 8));
        ega_.port_out(0x3D4, 0x0F);
        ega_.port_out(0x3D5, static_cast<std::uint8_t>(row * 80 + col));
    };
    auto text_cell = [this](int row, int col) { return &mem_[0xB8000 + (row * 80 + col) * 2]; };
    auto scroll_up = [&](int lines, int top, int left, int bottom, int right, std::uint8_t attr) {
        for (int r = top; r <= bottom; ++r)
            for (int c = left; c <= right; ++c) {
                std::uint8_t* dst = text_cell(r, c);
                const int src = r + lines;
                if (lines == 0 || src > bottom) {
                    dst[0] = ' ', dst[1] = attr;
                } else {
                    dst[0] = text_cell(src, c)[0], dst[1] = text_cell(src, c)[1];
                }
            }
    };
    auto teletype = [&](std::uint8_t ch) {
        auto [row, col] = cursor();
        if (ch == '\r') {
            col = 0;
        } else if (ch == '\n') {
            ++row;
        } else if (ch == 8) {
            if (col > 0) --col;
        } else if (ch == 7) {
            // bell: ignored
        } else {
            if (!ega_.graphics()) {
                text_cell(row, col)[0] = ch;
            }
            if (++col >= 80) col = 0, ++row;
        }
        if (row > 24) {
            if (!ega_.graphics()) scroll_up(1, 0, 0, 24, 79, 0x07);
            row = 24;
        }
        set_cursor(row, col);
    };

    switch (ah) {
        case 0x00:
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "INT 10h set mode %02Xh from %04X:%04X", al, rd16(cpu.s[SS], static_cast<std::uint16_t>(cpu.r[SP] + 2)),
                    rd16(cpu.s[SS], cpu.r[SP]));
            ega_.set_mode(al & 0x7F, !(al & 0x80));
            mem_[0x449] = al & 0x7F;
            if (!(al & 0x80) && !ega_.graphics())
                for (int i = 0; i < 80 * 25; ++i) mem_[0xB8000 + i * 2] = ' ', mem_[0xB8000 + i * 2 + 1] = 0x07;
            set_cursor(0, 0);
            break;
        case 0x01: break;  // cursor shape
        case 0x02: set_cursor(cpu.get8(6), cpu.get8(2)); break;
        case 0x03: {
            auto [row, col] = cursor();
            cpu.set8(6, static_cast<std::uint8_t>(row));
            cpu.set8(2, static_cast<std::uint8_t>(col));
            cpu.r[CX] = 0x0B0C;
            break;
        }
        case 0x05:
            ega_.set_page(al);
            mem_[0x462] = al;
            break;
        case 0x06:
        case 0x07: {
            const int lines = al, top = cpu.get8(5), left = cpu.get8(1), bottom = cpu.get8(6), right = cpu.get8(2);
            if (ega_.graphics()) break;
            if (ah == 0x06) {
                scroll_up(lines, top, left, std::min(bottom, 24), std::min(right, 79), cpu.get8(7));
            } else {
                for (int r = std::min(bottom, 24); r >= top; --r)
                    for (int c = left; c <= std::min(right, 79); ++c) {
                        std::uint8_t* dst = text_cell(r, c);
                        const int src = r - lines;
                        if (lines == 0 || src < top) dst[0] = ' ', dst[1] = cpu.get8(7);
                        else dst[0] = text_cell(src, c)[0], dst[1] = text_cell(src, c)[1];
                    }
            }
            break;
        }
        case 0x08: {
            auto [row, col] = cursor();
            cpu.r[AX] = static_cast<std::uint16_t>(text_cell(row, col)[0] | text_cell(row, col)[1] << 8);
            break;
        }
        case 0x09:
        case 0x0A: {
            auto [row, col] = cursor();
            for (int i = 0; i < cpu.r[CX] && !ega_.graphics(); ++i) {
                const int c = col + i;
                if (c >= 80 * 25) break;
                std::uint8_t* cell = text_cell(row + c / 80, c % 80);
                cell[0] = al;
                if (ah == 0x09) cell[1] = cpu.get8(3);
            }
            break;
        }
        case 0x0E: teletype(al); break;
        case 0x0F:
            cpu.set8(0, ega_.mode());
            cpu.set8(4, 80);
            cpu.set8(7, 0);
            break;
        case 0x10:
            if (al == 0x00) ega_.set_palette(cpu.get8(3), cpu.get8(7));
            else if (al == 0x01) ega_.set_border(cpu.get8(7));
            else if (al == 0x02)
                for (int i = 0; i < 17; ++i) {
                    const std::uint8_t v = rd8(cpu.s[ES], static_cast<std::uint16_t>(cpu.r[DX] + i));
                    if (i < 16) ega_.set_palette(i, v);
                    else ega_.set_border(v);
                }
            break;
        case 0x11:
            if (al == 0x30) {
                const std::uint8_t which = cpu.get8(7);
                cpu.s[ES] = kBiosSeg;
                cpu.r[BP] = which == 4 ? kFontHighOff : kFontOff;
                cpu.r[CX] = 14;
                cpu.set8(2, 24);
                if (warned_.insert(0x11300 + which).second) SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "INT 10h AX=1130h font %d", which);
            }
            break;
        case 0x12:
            if (cpu.get8(3) == 0x10) {  // EGA information: colour, 256 KB, switches 9
                cpu.r[BX] = 0x0003;
                cpu.r[CX] = 0x0009;
            }
            break;
        case 0x13: {  // write string
            const std::uint16_t seg = cpu.s[ES];
            const int row0 = cpu.get8(6), col0 = cpu.get8(2);
            set_cursor(row0, col0);
            for (int i = 0; i < cpu.r[CX]; ++i) {
                const std::uint8_t ch = rd8(seg, static_cast<std::uint16_t>(cpu.r[BP] + i * ((al & 2) ? 2 : 1)));
                auto [row, col] = cursor();
                if (!ega_.graphics() && ch >= 0x20) text_cell(row, col)[1] = cpu.get8(3);
                teletype(ch);
            }
            if (!(al & 1)) set_cursor(row0, col0);
            break;
        }
        case 0x1A: break;  // VGA display combination: unsupported on EGA, so AL is not set to 1Ah
        default:
            if (warned_.insert(0x10000 + ah).second) SDL_Log("unhandled INT 10h AH=%02Xh", ah);
            break;
    }
    return true;
}

bool Machine::int16() {
    const std::uint8_t ah = cpu.get8(4) & 0x0F;  // treat enhanced 10h-12h like 00h-02h
    switch (ah) {
        case 0x00:
            if (keys_.empty()) {
                waiting_key_ = true;
                budget = 0;
                return false;
            }
            cpu.r[AX] = keys_.front();
            keys_.pop_front();
            if (log_keys) SDL_Log("ReadKey -> %04X from %04X:%04X", cpu.r[AX], rd16(cpu.s[SS], static_cast<std::uint16_t>(cpu.r[SP] + 2)), rd16(cpu.s[SS], cpu.r[SP]));
            break;
        case 0x01:
            if (keys_.empty()) {
                cpu.zf = true;
                waiting_key_ = true;
                budget = std::min<std::int64_t>(budget, 64);  // polling: let the host catch up soon
            } else {
                cpu.zf = false;
                cpu.r[AX] = keys_.front();
            }
            break;
        case 0x02: cpu.set8(0, shift_flags_); break;
        default: break;
    }
    return true;
}

bool Machine::int1a() {
    if (cpu.get8(4) == 0x00) {
        read_ticks();
        cpu.r[CX] = static_cast<std::uint16_t>(ticks_ >> 16);
        cpu.r[DX] = static_cast<std::uint16_t>(ticks_);
        cpu.set8(0, 0);
    }
    return true;
}

}  // namespace trek::dos

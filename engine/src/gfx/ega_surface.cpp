#include "gfx/ega_surface.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace trek {

std::uint8_t EgaSurface::get(int x, int y) const {
    if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return 0;
    return pixels_[y * kWidth + x];
}

void EgaSurface::put(int x, int y, std::uint8_t color) {
    if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
    pixels_[y * kWidth + x] = color & 0x0F;
}

void EgaSurface::clear(std::uint8_t color) { std::fill(pixels_.begin(), pixels_.end(), color & 0x0F); }

void EgaSurface::line(int x1, int y1, int x2, int y2, std::uint8_t color) {
    const int dx = std::abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    const int dy = -std::abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        const int e2 = 2 * err;
        if (e2 >= dy) err += dy, x1 += sx;
        if (e2 <= dx) err += dx, y1 += sy;
    }
}

void EgaSurface::rectangle(int x1, int y1, int x2, int y2, std::uint8_t color) {
    line(x1, y1, x2, y1, color);
    line(x2, y1, x2, y2, color);
    line(x2, y2, x1, y2, color);
    line(x1, y2, x1, y1, color);
}

void EgaSurface::bar(int x1, int y1, int x2, int y2, std::uint8_t color) {
    if (x1 > x2) std::swap(x1, x2);
    if (y1 > y2) std::swap(y1, y2);
    x1 = std::max(x1, 0), y1 = std::max(y1, 0);
    x2 = std::min(x2, kWidth - 1), y2 = std::min(y2, kHeight - 1);
    for (int y = y1; y <= y2; ++y)
        std::fill(pixels_.begin() + y * kWidth + x1, pixels_.begin() + y * kWidth + x2 + 1,
                  static_cast<std::uint8_t>(color & 0x0F));
}

const std::array<std::uint32_t, 16>& EgaSurface::default_palette() {
    // EGA 64-colour values 0,1,2,3,4,5,20,7,56..63 as RGB.
    static const std::array<std::uint32_t, 16> p = {
        0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA, 0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
        0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF, 0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF,
    };
    return p;
}

void EgaSurface::to_argb(std::span<std::uint32_t> out, const std::array<std::uint32_t, 16>& palette) const {
    if (out.size() < pixels_.size()) throw std::length_error("to_argb: output buffer too small");
    std::transform(pixels_.begin(), pixels_.end(), out.begin(), [&](std::uint8_t c) { return palette[c]; });
}

}  // namespace trek

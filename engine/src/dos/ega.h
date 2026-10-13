#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace trek::dos {

// IBM EGA with 256 KB: four 64 KB bit planes behind the graphics controller,
// as programmed by the BGI EGAVGA driver. Supports mode 10h (640x350x16
// graphics, two pages) and the colour text modes (for the "boss" screen).
class Ega {
public:
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 350;

    Ega();

    // CPU access to A000:0000-A000:FFFF in graphics modes.
    std::uint8_t read(std::uint16_t off);
    void write(std::uint16_t off, std::uint8_t v);

    void port_out(std::uint16_t port, std::uint8_t v);
    std::uint8_t port_in(std::uint16_t port);

    // BIOS-level operations (INT 10h).
    void set_mode(std::uint8_t mode, bool clear);
    std::uint8_t mode() const { return mode_; }
    bool graphics() const { return mode_ >= 0x0D; }
    void set_palette(int index, std::uint8_t ega_color) { attr_[index & 0x0F] = ega_color & 0x3F; }
    std::uint8_t palette(int index) const { return attr_[index & 0x0F]; }
    void set_border(std::uint8_t ega_color) { border_ = ega_color & 0x3F; }
    void reset_palette();

    // 32-bit ARGB, kWidth x kHeight. text_mem is B800:0000 for text modes,
    // font8x8 the ROM font (characters 0-127).
    void render(std::span<std::uint32_t> argb, const std::uint8_t* text_mem, const std::uint8_t* font8x8,
                bool blink_on) const;

    // 6-bit EGA colour (rgbRGB) to ARGB.
    static std::uint32_t rgb(std::uint8_t ega_color);

    // Direct plane access for tests and debugging.
    std::uint8_t plane(int p, std::uint16_t off) const { return planes_[p][off]; }
    int pixel(int x, int y) const;  // colour index at (x, y) on the visible page
    std::uint16_t start_address() const { return start_; }
    // INT 10h AH=05h: show display page n (8000h bytes per page in mode 10h, 800h cells in text modes).
    void set_page(int n) { start_ = static_cast<std::uint16_t>(graphics() ? n * 0x8000 : n * 0x800); }

private:
    std::array<std::vector<std::uint8_t>, 4> planes_;
    std::array<std::uint8_t, 4> latch_{};
    std::uint8_t mode_ = 3;

    std::uint8_t seq_index_ = 0, map_mask_ = 0x0F;
    std::uint8_t gc_index_ = 0;
    std::uint8_t set_reset_ = 0, enable_set_reset_ = 0, color_compare_ = 0, rotate_ = 0, read_map_ = 0;
    std::uint8_t gc_mode_ = 0, color_dont_care_ = 0x0F, bit_mask_ = 0xFF;
    std::uint8_t crtc_index_ = 0;
    std::uint16_t start_ = 0;
    std::uint16_t cursor_ = 0;
    bool attr_flip_ = false;
    std::uint8_t attr_index_ = 0;
    std::array<std::uint8_t, 16> attr_{};
    std::uint8_t border_ = 0;
    bool retrace_ = false;
};

}  // namespace trek::dos

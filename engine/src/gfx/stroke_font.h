#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gfx/ega_surface.h"

namespace trek {

// A Borland BGI stroke font (.CHR), e.g. TRIP.CHR linked into TREK.EXE.
class StrokeFont {
public:
    enum class Op : std::uint8_t { Move, Draw };
    struct Stroke {
        Op op;
        std::int8_t x, y;  // y up, relative to the baseline
    };

    static StrokeFont parse(std::span<const std::byte> data);  // throws ReadError
    // Finds the first "PK\x08\x08BGI " font in a larger buffer (an MZ load image).
    static StrokeFont find_in(std::span<const std::byte> image);

    const std::string& name() const { return name_; }
    int top() const { return top_; }
    int bottom() const { return bottom_; }
    int glyph_count() const { return static_cast<int>(glyphs_.size()); }
    std::span<const Stroke> glyph(unsigned char c) const;
    int width(unsigned char c) const;

    // Text width in pixels at a scale of mul/div (BGI user char size).
    int text_width(std::string_view text, int mul = 1, int div = 1) const;
    // Draws horizontally with the top-left of the text box at (x, y),
    // as BGI does with the default LeftText/TopText justification.
    void draw(EgaSurface& s, int x, int y, std::string_view text, std::uint8_t color, int mul = 1,
              int div = 1) const;

private:
    std::string name_;
    int first_ = 0, top_ = 0, bottom_ = 0;
    std::vector<std::uint8_t> widths_;
    std::vector<std::vector<Stroke>> glyphs_;
};

}  // namespace trek

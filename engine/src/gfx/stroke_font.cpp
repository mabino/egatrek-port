#include "gfx/stroke_font.h"

#include <algorithm>
#include <cstring>

#include "core/byte_reader.h"

namespace trek {

namespace {

constexpr char kMagic[] = "PK\x08\x08" "BGI ";

int s7(std::uint8_t v) {
    v &= 0x7F;
    return (v & 0x40) ? v - 0x80 : v;
}

}  // namespace

StrokeFont StrokeFont::parse(std::span<const std::byte> data) {
    ByteReader r(data);
    if (r.size() < 8 || std::memcmp(r.bytes(8).data(), kMagic, 8) != 0) throw ReadError("not a BGI stroke font");
    while (r.u8() != 0x1A) {
        if (r.pos() > 0x80) throw ReadError("BGI font description too long");
    }
    const std::size_t header_size = r.u16();
    StrokeFont f;
    f.name_ = r.fixed_string(4);

    r.seek(header_size);
    if (r.u8() != '+') throw ReadError("stroke font header signature missing");
    const std::size_t n = r.u16();
    r.skip(1);
    f.first_ = r.u8();
    const std::size_t defs = r.u16();
    r.skip(1);  // scan flag
    f.top_ = static_cast<std::int8_t>(r.u8());
    r.skip(1);  // baseline, always 0
    f.bottom_ = static_cast<std::int8_t>(r.u8());
    r.skip(5);
    std::vector<std::uint16_t> offsets(n);
    for (auto& o : offsets) o = r.u16();
    for (std::size_t i = 0; i < n; ++i) f.widths_.push_back(r.u8());

    for (auto o : offsets) {
        r.seek(header_size + defs + o);
        auto& glyph = f.glyphs_.emplace_back();
        for (;;) {
            const std::uint8_t b1 = r.u8(), b2 = r.u8();
            const int op = ((b1 >> 7) << 1) | (b2 >> 7);
            if (op == 0) break;
            if (op == 1) continue;  // "scan" records are unused by BGI
            glyph.push_back({op == 2 ? Op::Move : Op::Draw, static_cast<std::int8_t>(s7(b1)),
                             static_cast<std::int8_t>(s7(b2))});
        }
    }
    return f;
}

StrokeFont StrokeFont::find_in(std::span<const std::byte> image) {
    const auto* begin = reinterpret_cast<const char*>(image.data());
    const auto* end = begin + image.size();
    const auto* hit = std::search(begin, end, kMagic, kMagic + 8);
    if (hit == end) throw ReadError("no BGI stroke font in image");
    return parse(image.subspan(static_cast<std::size_t>(hit - begin)));
}

std::span<const StrokeFont::Stroke> StrokeFont::glyph(unsigned char c) const {
    const int i = c - first_;
    if (i < 0 || i >= glyph_count()) return {};
    return glyphs_[i];
}

int StrokeFont::width(unsigned char c) const {
    const int i = c - first_;
    return (i < 0 || i >= glyph_count()) ? 0 : widths_[i];
}

int StrokeFont::text_width(std::string_view text, int mul, int div) const {
    int w = 0;
    for (unsigned char c : text) w += width(c);
    return w * mul / div;
}

void StrokeFont::draw(EgaSurface& s, int x, int y, std::string_view text, std::uint8_t color, int mul,
                      int div) const {
    const int baseline = y + top_ * mul / div;
    int pen = 0;  // unscaled advance, so rounding does not accumulate
    for (unsigned char c : text) {
        int cx = 0, cy = 0;
        for (const auto& st : glyph(c)) {
            const int nx = x + (pen + st.x) * mul / div;
            const int ny = baseline - st.y * mul / div;
            if (st.op == Op::Draw) s.line(cx, cy, nx, ny, color);
            cx = nx, cy = ny;
        }
        pen += width(c);
    }
}

}  // namespace trek

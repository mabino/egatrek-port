#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

// Little helpers for building synthetic binaries in tests (no game data needed).
struct Bytes {
    std::vector<std::byte> v;
    Bytes& u8(unsigned x) { v.push_back(std::byte(x & 0xFF)); return *this; }
    Bytes& u16(unsigned x) { return u8(x).u8(x >> 8); }
    Bytes& str(std::string_view s) { for (char c : s) u8(static_cast<unsigned char>(c)); return *this; }
    Bytes& pad_to(std::size_t n) { while (v.size() < n) u8(0); return *this; }
    Bytes& append(const Bytes& o) { v.insert(v.end(), o.v.begin(), o.v.end()); return *this; }
};

// A BGI stroke font with glyphs 'I' (vertical bar 0..10) and 'J' (no strokes).
inline Bytes make_font() {
    Bytes body;
    const unsigned n = 2;
    body.u8('+').u16(n).u8(0).u8('I').u16(16 + 3 * n).u8(0).u8(10).u8(0).u8(0xFD /* -3 */).pad_to(16);
    body.u16(0).u16(6);   // stroke offsets
    body.u8(3).u8(5);     // widths
    body.u8(0x80 | 1).u8(0).u8(0x80 | 1).u8(0x80 | 10).u8(0).u8(0);  // move(1,0) draw(1,10) end
    body.u8(0).u8(0);
    Bytes f;
    f.str("PK\x08\x08" "BGI Test\r\n").u8(0).u8(0x1A).u16(0x80).str("TEST").u16(static_cast<unsigned>(body.v.size()));
    f.u8(1).u8(0).pad_to(0x80).append(body);
    return f;
}

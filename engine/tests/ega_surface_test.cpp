#include <doctest/doctest.h>

#include <vector>

#include "gfx/ega_surface.h"

using trek::EgaSurface;

TEST_CASE("put/get clip and mask to 4 bits") {
    EgaSurface s;
    s.put(-1, 0, 5);
    s.put(640, 349, 5);
    s.put(639, 349, 0x1F);
    CHECK(s.get(639, 349) == 0x0F);
    CHECK(s.get(-1, 0) == 0);
}

TEST_CASE("lines include both endpoints in any direction") {
    EgaSurface s;
    s.line(10, 10, 4, 7, 3);
    CHECK(s.get(10, 10) == 3);
    CHECK(s.get(4, 7) == 3);
    int count = 0;
    for (int x = 0; x < 20; ++x)
        for (int y = 0; y < 20; ++y) count += s.get(x, y) == 3;
    CHECK(count == 7);  // one pixel per step along the major axis
}

TEST_CASE("bar fills inclusively and clips") {
    EgaSurface s;
    s.bar(5, 5, 2, 3, 9);
    CHECK(s.get(2, 3) == 9);
    CHECK(s.get(5, 5) == 9);
    CHECK(s.get(6, 5) == 0);
    s.bar(-10, -10, 1000, 1000, 1);
    CHECK(s.get(0, 0) == 1);
    CHECK(s.get(639, 349) == 1);
}

TEST_CASE("rectangle draws outline only") {
    EgaSurface s;
    s.rectangle(1, 1, 4, 4, 2);
    CHECK(s.get(1, 1) == 2);
    CHECK(s.get(4, 2) == 2);
    CHECK(s.get(2, 2) == 0);
}

TEST_CASE("palette conversion") {
    EgaSurface s;
    s.put(0, 0, 14);
    std::vector<std::uint32_t> out(EgaSurface::kWidth * EgaSurface::kHeight);
    s.to_argb(out, EgaSurface::default_palette());
    CHECK(out[0] == 0xFFFFFF55u);  // yellow
    CHECK(out[1] == 0xFF000000u);
    std::vector<std::uint32_t> tiny(4);
    CHECK_THROWS(s.to_argb(tiny, EgaSurface::default_palette()));
    CHECK(EgaSurface::default_palette()[6] == 0xFFAA5500u);  // EGA brown, not dark yellow
}

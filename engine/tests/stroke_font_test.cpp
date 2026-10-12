#include <doctest/doctest.h>

#include "core/byte_reader.h"
#include "core/data_path.h"
#include "core/mz_image.h"
#include "gfx/stroke_font.h"
#include "test_bytes.h"

using trek::StrokeFont;

TEST_CASE("parses a synthetic stroke font") {
    auto raw = make_font();
    auto f = StrokeFont::parse(raw.v);
    CHECK(f.name() == "TEST");
    CHECK(f.top() == 10);
    CHECK(f.bottom() == -3);
    REQUIRE(f.glyph('I').size() == 2);
    CHECK(f.glyph('I')[0].op == StrokeFont::Op::Move);
    CHECK(f.glyph('I')[1].y == 10);
    CHECK(f.glyph('J').empty());
    CHECK(f.glyph('Z').empty());
    CHECK(f.width('I') == 3);
    CHECK(f.text_width("IJI") == 11);
    CHECK(f.text_width("IJI", 2, 1) == 22);
}

TEST_CASE("draws strokes below the top of the text box") {
    auto raw = make_font();
    auto f = StrokeFont::parse(raw.v);
    trek::EgaSurface s;
    f.draw(s, 100, 50, "JI", 12);
    // 'J' advances 5; the 'I' stroke runs from the baseline (y=60) up to the top (y=50) at x=106.
    CHECK(s.get(106, 60) == 12);
    CHECK(s.get(106, 50) == 12);
    CHECK(s.get(106, 49) == 0);
    CHECK(s.get(101, 55) == 0);
}

TEST_CASE("find_in locates a font inside a larger image") {
    Bytes image;
    image.str("junk junk").append(make_font());
    CHECK(StrokeFont::find_in(image.v).name() == "TEST");
    Bytes none;
    none.str("no font here").pad_to(64);
    CHECK_THROWS_AS(StrokeFont::find_in(none.v), trek::ReadError);
}

TEST_CASE("real Triplex font from TREK.EXE (skipped without TREK_DATA)") {
    auto data = trek::DataPath::from_environment();
    if (!data) return;
    auto mz = trek::MzImage::load(*data->resolve("TREK.EXE"));
    auto f = StrokeFont::find_in(mz.image());
    CHECK(f.name() == "TRIP");
    CHECK(f.glyph_count() == 95);
    CHECK(f.top() == 21);
    CHECK(f.bottom() == -7);
    CHECK(f.glyph('A')[0].x == 9);
}

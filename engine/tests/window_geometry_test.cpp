#include <doctest/doctest.h>

#include "app/window_geometry.h"

TEST_CASE("WindowGeometry default size matches 4:3 native dimensions") {
    const auto [w, h] = trek::WindowGeometry::default_window_size();
    CHECK(w == 640);
    CHECK(h == 480);
    CHECK(trek::WindowGeometry::kAspectRatio == doctest::Approx(4.0f / 3.0f));
    CHECK_FALSE(trek::WindowGeometry::has_side_letterboxing(w, h));
}

TEST_CASE("WindowGeometry detects side letterboxing on non-4:3 aspect ratios") {
    // Exact 4:3 resolutions should have no side letterboxing
    CHECK_FALSE(trek::WindowGeometry::has_side_letterboxing(640, 480));
    CHECK_FALSE(trek::WindowGeometry::has_side_letterboxing(800, 600));
    CHECK_FALSE(trek::WindowGeometry::has_side_letterboxing(1024, 768));
    CHECK_FALSE(trek::WindowGeometry::has_side_letterboxing(1280, 960));

    // Wider resolutions (e.g. clamped heights on laptops or 16:9/16:10 aspect ratios) produce side letterboxing
    CHECK(trek::WindowGeometry::has_side_letterboxing(1280, 850));
    CHECK(trek::WindowGeometry::has_side_letterboxing(1280, 800));
    CHECK(trek::WindowGeometry::has_side_letterboxing(1280, 720));
    CHECK(trek::WindowGeometry::has_side_letterboxing(1920, 1080));
    CHECK(trek::WindowGeometry::has_side_letterboxing(640, 350));
}

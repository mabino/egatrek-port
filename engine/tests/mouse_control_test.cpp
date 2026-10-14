#include <doctest/doctest.h>

#include "app/mouse_control.h"
#include "dos/machine.h"

TEST_SUITE("mouse_control") {

TEST_CASE("Sector map coordinate mapping") {
    // 8x8 grid within [30, 158) x [18, 130)
    // Test each cell (row 1..8, col 1..8)
    for (int r = 1; r <= 8; ++r) {
        const float y = 18.0f + (r - 1) * 14.0f + 7.0f;
        for (int c = 1; c <= 8; ++c) {
            const float x = 30.0f + (c - 1) * 16.0f + 8.0f;
            auto mapped = trek::mouse::map_sector_coordinates(x, y);
            REQUIRE(mapped.has_value());
            CHECK_EQ(mapped->first, r);
            CHECK_EQ(mapped->second, c);
        }
    }

    // Edge and out-of-bounds testing
    CHECK_FALSE(trek::mouse::map_sector_coordinates(29.9f, 50.0f).has_value());
    CHECK_FALSE(trek::mouse::map_sector_coordinates(158.0f, 50.0f).has_value());
    CHECK_FALSE(trek::mouse::map_sector_coordinates(50.0f, 17.9f).has_value());
    CHECK_FALSE(trek::mouse::map_sector_coordinates(50.0f, 130.0f).has_value());
}

TEST_CASE("Quadrant map coordinate mapping") {
    // 8x8 grid within [364, 620) x [35, 123)
    // Test each cell (row 1..8, col 1..8)
    for (int r = 1; r <= 8; ++r) {
        const float y = 35.0f + (r - 1) * 11.0f + 5.5f;
        for (int c = 1; c <= 8; ++c) {
            const float x = 364.0f + (c - 1) * 32.0f + 16.0f;
            auto mapped = trek::mouse::map_quadrant_coordinates(x, y);
            REQUIRE(mapped.has_value());
            CHECK_EQ(mapped->first, r);
            CHECK_EQ(mapped->second, c);
        }
    }

    // Edge and out-of-bounds testing
    CHECK_FALSE(trek::mouse::map_quadrant_coordinates(363.9f, 50.0f).has_value());
    CHECK_FALSE(trek::mouse::map_quadrant_coordinates(620.0f, 50.0f).has_value());
    CHECK_FALSE(trek::mouse::map_quadrant_coordinates(400.0f, 34.9f).has_value());
    CHECK_FALSE(trek::mouse::map_quadrant_coordinates(400.0f, 123.0f).has_value());
}

TEST_CASE("Message box mapping") {
    // Message box bounds x in [320, 639]
    CHECK_EQ(trek::mouse::map_message_box(400.0f, 140.0f), 1);
    CHECK_EQ(trek::mouse::map_message_box(400.0f, 186.0f), 1);
    CHECK_EQ(trek::mouse::map_message_box(400.0f, 187.0f), 2);
    CHECK_EQ(trek::mouse::map_message_box(400.0f, 234.0f), 2);
    CHECK_EQ(trek::mouse::map_message_box(400.0f, 235.0f), 3);
    CHECK_EQ(trek::mouse::map_message_box(400.0f, 282.0f), 3);
    CHECK_EQ(trek::mouse::map_message_box(400.0f, 283.0f), 4);
    CHECK_EQ(trek::mouse::map_message_box(400.0f, 349.0f), 4);

    // Out of bounds
    CHECK_FALSE(trek::mouse::map_message_box(319.0f, 200.0f).has_value());
    CHECK_FALSE(trek::mouse::map_message_box(640.0f, 200.0f).has_value());
    CHECK_FALSE(trek::mouse::map_message_box(400.0f, 138.0f).has_value());
    CHECK_FALSE(trek::mouse::map_message_box(400.0f, 350.0f).has_value());
}

TEST_CASE("Parse coordinate input") {
    CHECK_EQ(trek::mouse::parse_coordinate_input("3,5"), "3,5");
    CHECK_EQ(trek::mouse::parse_coordinate_input("3, 5"), "3,5");
    CHECK_EQ(trek::mouse::parse_coordinate_input("3 5"), "3,5");
    CHECK_EQ(trek::mouse::parse_coordinate_input(" 3 - 5 "), "3,5");
    CHECK_EQ(trek::mouse::parse_coordinate_input("6,2,3,5"), "6,2,3,5");
    CHECK_EQ(trek::mouse::parse_coordinate_input("6 2 3 5"), "6,2,3,5");
    CHECK_EQ(trek::mouse::parse_coordinate_input(" 6, 2, 3, 5 "), "6,2,3,5");

    // Invalid coordinates out of 1..8 range
    CHECK_FALSE(trek::mouse::parse_coordinate_input("0,5").has_value());
    CHECK_FALSE(trek::mouse::parse_coordinate_input("9,5").has_value());
    CHECK_FALSE(trek::mouse::parse_coordinate_input("3,9").has_value());
    CHECK_FALSE(trek::mouse::parse_coordinate_input("6,2,0,5").has_value());
    CHECK_FALSE(trek::mouse::parse_coordinate_input("6,2,3,9").has_value());

    // Invalid count
    CHECK_FALSE(trek::mouse::parse_coordinate_input("").has_value());
    CHECK_FALSE(trek::mouse::parse_coordinate_input("3").has_value());
    CHECK_FALSE(trek::mouse::parse_coordinate_input("3,4,5").has_value());
    CHECK_FALSE(trek::mouse::parse_coordinate_input("1,2,3,4,5").has_value());
    CHECK_FALSE(trek::mouse::parse_coordinate_input("foo").has_value());
}

TEST_CASE("Format move command from map clicks") {
    // Quadrant click formats \x1bM <row>,<col>,
    CHECK_EQ(trek::mouse::format_move_command("", true, 6, 2), "\x1bM 6,2,");
    CHECK_EQ(trek::mouse::format_move_command("M 3,4", true, 6, 2), "\x1bM 6,2,");

    // Sector click with no previous quadrant sets sector \x1bM <row>,<col>
    CHECK_EQ(trek::mouse::format_move_command("", false, 3, 5), "\x1bM 3,5");
    CHECK_EQ(trek::mouse::format_move_command("M 1,1", false, 3, 5), "\x1bM 3,5");

    // Sector click when previous command specified quadrant (ends with comma) appends sector
    CHECK_EQ(trek::mouse::format_move_command("M 6,2,", false, 3, 5), "\x1bM 6,2,3,5");
    CHECK_EQ(trek::mouse::format_move_command("m 6, 2,", false, 3, 5), "\x1bM 6,2,3,5");
    // Sector click when quadrant + sector was already specified replaces sector
    CHECK_EQ(trek::mouse::format_move_command("M 6,2,1,1", false, 3, 5), "\x1bM 6,2,3,5");
}

TEST_CASE("Format coordinate command") {
    CHECK_EQ(trek::mouse::format_coordinate_command("", "3,5"), "\x1bM 3,5");
    CHECK_EQ(trek::mouse::format_coordinate_command("M 6,2,", "3,5"), "\x1bM 6,2,3,5");
    CHECK_EQ(trek::mouse::format_coordinate_command("M 6,2,1,1", "3,5"), "\x1bM 6,2,3,5");
    CHECK_EQ(trek::mouse::format_coordinate_command("", "6,2,3,5"), "\x1bM 6,2,3,5");
}

TEST_CASE("Machine startup settings and title screen state") {
    std::vector<std::uint8_t> image(0x4000, 0);
    trek::dos::Program prog{image, 0x1000, 0x1000, 0, 0x1000, 0x200, 0, {}, {}, {}, {}};
    trek::dos::Machine m(prog, {});

    // Initial state: at title screen
    CHECK(m.is_at_title_screen());

    // Advancing past title screen
    m.mark_title_screen_passed();
    CHECK_FALSE(m.is_at_title_screen());

    // Re-entering title screen
    m.mark_title_screen_entered();
    CHECK(m.is_at_title_screen());

    // StartupSettings configuration
    trek::dos::Machine::StartupSettings s;
    s.skip_briefing = true;
    s.default_difficulty = 3;
    s.default_password = "spock";
    m.set_startup_settings(s);

    CHECK(m.startup_settings().skip_briefing);
    CHECK_EQ(m.startup_settings().default_difficulty, 3);
    CHECK_EQ(m.startup_settings().default_password, "spock");
}

}  // TEST_SUITE("mouse_control")

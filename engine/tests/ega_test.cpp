#include <doctest/doctest.h>

#include <vector>

#include "dos/ega.h"
#include "dos/keymap.h"

using trek::dos::Ega;

namespace {
void gc(Ega& e, int index, int value) {
    e.port_out(0x3CE, static_cast<std::uint8_t>(index));
    e.port_out(0x3CF, static_cast<std::uint8_t>(value));
}
void map_mask(Ega& e, int value) {
    e.port_out(0x3C4, 2);
    e.port_out(0x3C5, static_cast<std::uint8_t>(value));
}
}  // namespace

TEST_CASE("write mode 0 with set/reset and bit mask, as BGI draws pixels") {
    Ega e;
    e.set_mode(0x10, true);
    gc(e, 0, 0x0E);  // set/reset colour 14
    gc(e, 1, 0x0F);  // enable for all planes
    gc(e, 8, 0x80);  // leftmost pixel only
    e.read(0);       // load latches
    e.write(0, 0xFF);
    CHECK(e.pixel(0, 0) == 14);
    CHECK(e.pixel(1, 0) == 0);
}

TEST_CASE("map mask limits the planes written") {
    Ega e;
    e.set_mode(0x10, true);
    map_mask(e, 0x01);
    e.write(80, 0xFF);  // row 1
    CHECK(e.pixel(0, 1) == 1);
    CHECK(e.plane(1, 80) == 0);
}

TEST_CASE("write mode 1 copies latches (BGI GetImage/PutImage, scrolling)") {
    Ega e;
    e.set_mode(0x10, true);
    gc(e, 0, 0x05);
    gc(e, 1, 0x0F);
    e.write(10, 0xFF);  // byte 10 = colour 5 in all 8 pixels
    gc(e, 1, 0x00);
    gc(e, 5, 0x01);  // write mode 1
    e.read(10);
    e.write(20, 0x00);
    CHECK(e.pixel(20 * 8, 0) == 5);
}

TEST_CASE("write mode 2 and logical functions") {
    Ega e;
    e.set_mode(0x10, true);
    gc(e, 5, 0x02);  // write mode 2: CPU byte is a colour
    e.read(0);
    e.write(0, 0x09);
    CHECK(e.pixel(3, 0) == 9);
    gc(e, 3, 0x18);  // XOR with latches
    e.read(0);
    e.write(0, 0x0F);
    CHECK(e.pixel(3, 0) == 6);
}

TEST_CASE("read mode 1: colour compare") {
    Ega e;
    e.set_mode(0x10, true);
    gc(e, 5, 0x02);
    gc(e, 8, 0xF0);
    e.read(0);
    e.write(0, 0x03);  // left 4 pixels colour 3
    gc(e, 8, 0xFF);
    gc(e, 5, 0x08);  // read mode 1
    gc(e, 2, 0x03);  // compare against colour 3
    CHECK(e.read(0) == 0xF0);
}

TEST_CASE("page flipping and palette") {
    Ega e;
    e.set_mode(0x10, true);
    map_mask(e, 0x0F);
    e.write(0x8000, 0xFF);
    CHECK(e.pixel(0, 0) == 0);
    e.set_page(1);
    CHECK(e.pixel(0, 0) == 15);
    std::vector<std::uint32_t> argb(Ega::kWidth * Ega::kHeight);
    e.set_palette(15, 0x3C);  // bright red, intensity bits
    e.render(argb, nullptr, nullptr, true);
    CHECK(argb[0] == 0xFFFF5555u);
    CHECK(Ega::rgb(0x14) == 0xFFAA5500u);  // brown
}

TEST_CASE("input status register toggles retrace so polling loops end") {
    Ega e;
    const auto a = e.port_in(0x3DA), b = e.port_in(0x3DA);
    CHECK((a & 8) != (b & 8));
}

TEST_CASE("key scripts") {
    using trek::dos::BiosKey;
    auto keys = trek::dos::parse_script("Ab\n{F1}{SF2}{UP}{WAIT 2}{BOGUS}x");
    REQUIRE(keys.size() == 9);
    CHECK(keys[0] == BiosKey{0x1E, 'A'});
    CHECK(keys[1] == BiosKey{0x30, 'b'});
    CHECK(keys[2] == BiosKey{0x1C, 0x0D});
    CHECK(keys[3] == BiosKey{0x3B, 0});
    CHECK(keys[4] == BiosKey{0x55, 0});
    CHECK(keys[5] == BiosKey{0x48, 0});
    CHECK(keys[6] == BiosKey{});
    CHECK(keys[7] == BiosKey{});
    CHECK(keys[8] == BiosKey{0x2D, 'x'});
    CHECK(trek::dos::key_for_char('?')->scan == 0x35);
    CHECK_FALSE(trek::dos::key_for_name("F11").has_value());
}

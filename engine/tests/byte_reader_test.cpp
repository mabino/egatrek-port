#include <doctest/doctest.h>

#include <array>

#include "core/byte_reader.h"

using trek::ByteReader;

namespace {
template <std::size_t N>
std::array<std::byte, N> bytes(const unsigned char (&raw)[N]) {
    std::array<std::byte, N> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = std::byte{raw[i]};
    return out;
}
}  // namespace

TEST_CASE("reads little-endian integers and floats") {
    const unsigned char raw[] = {'M', 'D', 'L', 'S', 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x00, 0x00, 0xa0, 0x40};
    auto data = bytes(raw);
    ByteReader r(data);
    CHECK(r.tag() == "MDLS");
    CHECK(r.u16() == 0x1234);
    CHECK(r.u32() == 0x12345678u);
    CHECK(r.f32() == 5.0f);
    CHECK(r.remaining() == 0);
}

TEST_CASE("fixed strings stop at NUL") {
    const unsigned char raw[] = {'a', 'b', 0, 'z', 'c'};
    auto data = bytes(raw);
    ByteReader r(data);
    CHECK(r.fixed_string(4) == "ab");
    CHECK(r.pos() == 4);
    CHECK(r.fixed_string(1) == "c");
}

TEST_CASE("overruns throw instead of reading past the buffer") {
    const unsigned char raw[] = {1, 2, 3};
    auto data = bytes(raw);
    ByteReader r(data);
    CHECK_THROWS_AS(r.u32(), trek::ReadError);
    CHECK(r.pos() == 0);
    CHECK_THROWS_AS(r.seek(4), trek::ReadError);
    r.seek(3);
    CHECK_THROWS_AS(r.u8(), trek::ReadError);
}

TEST_CASE("negative i32") {
    const unsigned char raw[] = {0xff, 0xff, 0xff, 0xff};
    auto data = bytes(raw);
    ByteReader r(data);
    CHECK(r.i32() == -1);
}

#include <doctest/doctest.h>

#include <cstdlib>

#include "core/byte_reader.h"
#include "core/data_path.h"
#include "core/mz_image.h"
#include "test_bytes.h"

namespace {
Bytes make_mz(std::size_t image_size) {
    const std::size_t total = 32 + image_size;
    Bytes b;
    b.str("MZ").u16(total % 512).u16((total + 511) / 512).u16(0).u16(2);
    b.u16(0).u16(0xFFFF).u16(0).u16(0x100).u16(0).u16(0x1234).u16(0x0002).u16(0x1C).u16(0).pad_to(32);
    for (std::size_t i = 0; i < image_size; ++i) b.u8(static_cast<unsigned>(i));
    return b;
}
}  // namespace

TEST_CASE("parses the load image and entry point") {
    auto raw = make_mz(64);
    auto mz = trek::MzImage::parse(raw.v);
    CHECK(mz.image().size() == 64);
    CHECK(mz.entry_cs() == 2);
    CHECK(mz.entry_ip() == 0x1234);
    auto span = mz.at(1, 2, 3);  // linear 18
    CHECK(std::to_integer<int>(span[0]) == 18);
    CHECK_THROWS_AS(mz.at(3, 15, 2), trek::ReadError);
}

TEST_CASE("rejects non-MZ and truncated files") {
    Bytes junk;
    junk.str("PE").pad_to(64);
    CHECK_THROWS_AS(trek::MzImage::parse(junk.v), trek::ReadError);
    auto raw = make_mz(64);
    raw.v.resize(80);
    CHECK_THROWS_AS(trek::MzImage::parse(raw.v), trek::ReadError);
}

TEST_CASE("real TREK.EXE (skipped without TREK_DATA)") {
    auto data = trek::DataPath::from_environment();
    if (!data) return;
    auto mz = trek::MzImage::load(*data->resolve("TREK.EXE"));
    CHECK(mz.image().size() == 132096);
    CHECK(mz.entry_cs() == 0);
    CHECK(mz.entry_ip() == 0x0EB0);
    CHECK(mz.relocation_count() == 3747);
}

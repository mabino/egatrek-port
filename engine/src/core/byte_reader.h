#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace trek {

struct ReadError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Bounds-checked little-endian reader for the game's on-disk formats.
// All original data is little-endian x86; this reader is endian-independent.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::byte> data) : data_(data) {}

    std::size_t pos() const { return pos_; }
    std::size_t size() const { return data_.size(); }
    std::size_t remaining() const { return data_.size() - pos_; }
    void seek(std::size_t pos);
    void skip(std::size_t n) { seek(pos_ + n); }

    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    float f32();
    std::span<const std::byte> bytes(std::size_t n);
    // Fixed-width field holding a NUL-padded string.
    std::string fixed_string(std::size_t n);
    // Four-character tag such as "TRIP".
    std::string tag() { return fixed_string(4); }

private:
    void need(std::size_t n) const;

    std::span<const std::byte> data_;
    std::size_t pos_ = 0;
};

}  // namespace trek

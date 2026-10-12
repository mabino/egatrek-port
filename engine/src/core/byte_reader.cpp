#include "core/byte_reader.h"

#include <bit>
#include <cstring>

namespace trek {

void ByteReader::need(std::size_t n) const {
    if (n > remaining()) {
        throw ReadError("read of " + std::to_string(n) + " bytes at offset " + std::to_string(pos_) +
                        " overruns buffer of " + std::to_string(data_.size()));
    }
}

void ByteReader::seek(std::size_t pos) {
    if (pos > data_.size()) {
        throw ReadError("seek to " + std::to_string(pos) + " past end " + std::to_string(data_.size()));
    }
    pos_ = pos;
}

std::uint8_t ByteReader::u8() {
    need(1);
    return std::to_integer<std::uint8_t>(data_[pos_++]);
}

std::uint16_t ByteReader::u16() {
    need(2);
    const std::uint16_t lo = u8();
    return static_cast<std::uint16_t>(lo | (u8() << 8));
}

std::uint32_t ByteReader::u32() {
    need(4);
    const std::uint32_t lo = u16();
    return lo | (static_cast<std::uint32_t>(u16()) << 16);
}

float ByteReader::f32() { return std::bit_cast<float>(u32()); }

std::span<const std::byte> ByteReader::bytes(std::size_t n) {
    need(n);
    auto out = data_.subspan(pos_, n);
    pos_ += n;
    return out;
}

std::string ByteReader::fixed_string(std::size_t n) {
    auto raw = bytes(n);
    const char* p = reinterpret_cast<const char*>(raw.data());
    return std::string(p, strnlen(p, n));
}

}  // namespace trek

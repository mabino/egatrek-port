#pragma once

#include <cstdint>

namespace trek::dos {

// 8x8 glyphs for characters 0-127, 8 bytes each, MSB = leftmost pixel.
extern const std::uint8_t kRomFont8x8[128 * 8];

}  // namespace trek::dos

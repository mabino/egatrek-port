#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trek::dos {

// A BIOS keystroke as INT 16h returns it: scan code and ASCII (0 for extended keys).
struct BiosKey {
    std::uint8_t scan = 0, ascii = 0;
    bool operator==(const BiosKey&) const = default;
};

// US-layout ASCII character to BIOS keystroke.
std::optional<BiosKey> key_for_char(char ch);

// Named keys: "F1".."F10", "SF1".."SF10" (shifted), "UP", "DOWN", "LEFT",
// "RIGHT", "HOME", "END", "PGUP", "PGDN", "INS", "DEL", "ESC", "ENTER", "BS", "TAB".
std::optional<BiosKey> key_for_name(std::string_view name);

// A scripted key sequence: plain characters, "\n" for Enter, "{NAME}" for
// named keys and "{WAIT n}" to idle n frames. Waits become {0, 0} keys
// repeated n times.
std::vector<BiosKey> parse_script(std::string_view script);

}  // namespace trek::dos

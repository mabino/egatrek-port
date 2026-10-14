#pragma once

#include <filesystem>

namespace trek {

// Validates whether the specified path points to a valid EGA Trek save file.
// EGA Trek saves are 227-line text files containing numerical ship/galaxy state,
// the ship name at line 25, and boolean flags at the end.
bool validate_save_file(const std::filesystem::path& path);

}  // namespace trek

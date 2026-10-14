#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace trek::mouse {

// Sector map is 8x8 inside EGA coords [30, 158) x [18, 130).
// Returns (row, col) both 1-indexed (1..8) if within bounds, std::nullopt otherwise.
std::optional<std::pair<int, int>> map_sector_coordinates(float ega_x, float ega_y);

// Quadrant map is 8x8 inside EGA coords [364, 620) x [35, 123).
// Returns (row, col) both 1-indexed (1..8) if within bounds, std::nullopt otherwise.
std::optional<std::pair<int, int>> map_quadrant_coordinates(float ega_x, float ega_y);

// Message boxes 1..4 inside EGA coords [320, 639] x [139, 350).
// Returns box index (1..4) if within a message box, std::nullopt otherwise.
std::optional<int> map_message_box(float ega_x, float ega_y);

// Parses user input string from menu dialog into standard comma-separated coordinate string.
// Accepts formats like "3,5", "3 5", "3, 5", "6,2,3,5", "6 2 3 5", etc.
// Returns normalized string (e.g. "3,5" or "6,2,3,5") if valid (all values 1..8), std::nullopt otherwise.
std::optional<std::string> parse_coordinate_input(std::string_view input);

// Formats a movement command given the current command prompt buffer and map click.
// If is_quadrant is true: returns "\x1bM <row>,<col>"
// If is_quadrant is false (sector click):
//   If current command specifies quadrant: returns "\x1bM <qr>,<qc>,<row>,<col>"
//   Otherwise: returns "\x1bM <row>,<col>"
std::string format_move_command(std::string_view current_cmd, bool is_quadrant, int row, int col);

// Formats a movement command given normalized coordinate input (e.g. "3,5" or "6,2,3,5").
std::string format_coordinate_command(std::string_view current_cmd, std::string_view coords);

// Formats keystrokes to send to Machine for a map click (supports single-click auto-fill and double-click immediate execution).
std::string format_move_action(bool at_move_prompt, std::string_view current_prompt_text,
                               bool is_quadrant, int row, int col, bool double_click,
                               std::pair<int, int> current_sector = {1, 1});

// Formats keystrokes to send for menu coordinate entry (Cmd+K / Cmd+J).
std::string format_coordinate_action(bool at_move_prompt, std::string_view current_prompt_text,
                                     std::string_view coords);

}  // namespace trek::mouse

#include "app/mouse_control.h"

#include <cctype>
#include <vector>

namespace trek::mouse {

namespace {

std::vector<int> extract_cmd_numbers(std::string_view cmd) {
    while (!cmd.empty() && std::isspace(static_cast<unsigned char>(cmd.front()))) {
        cmd.remove_prefix(1);
    }
    if (cmd.empty() || (cmd.front() != 'M' && cmd.front() != 'm')) {
        return {};
    }
    cmd.remove_prefix(1);
    std::vector<int> nums;
    std::size_t i = 0;
    while (i < cmd.size()) {
        while (i < cmd.size() && !std::isdigit(static_cast<unsigned char>(cmd[i]))) {
            ++i;
        }
        if (i < cmd.size()) {
            int val = 0;
            while (i < cmd.size() && std::isdigit(static_cast<unsigned char>(cmd[i]))) {
                val = val * 10 + (cmd[i] - '0');
                ++i;
            }
            nums.push_back(val);
        }
    }
    return nums;
}

}  // namespace

std::optional<std::pair<int, int>> map_sector_coordinates(float ega_x, float ega_y) {
    if (ega_x < 30.0f || ega_x >= 158.0f || ega_y < 18.0f || ega_y >= 130.0f) {
        return std::nullopt;
    }
    const int col = static_cast<int>((ega_x - 30.0f) / 16.0f) + 1;
    const int row = static_cast<int>((ega_y - 18.0f) / 14.0f) + 1;
    if (col < 1 || col > 8 || row < 1 || row > 8) {
        return std::nullopt;
    }
    return std::make_pair(row, col);
}

std::optional<std::pair<int, int>> map_quadrant_coordinates(float ega_x, float ega_y) {
    if (ega_x < 364.0f || ega_x >= 620.0f || ega_y < 35.0f || ega_y >= 123.0f) {
        return std::nullopt;
    }
    const int col = static_cast<int>((ega_x - 364.0f) / 32.0f) + 1;
    const int row = static_cast<int>((ega_y - 35.0f) / 11.0f) + 1;
    if (col < 1 || col > 8 || row < 1 || row > 8) {
        return std::nullopt;
    }
    return std::make_pair(row, col);
}

std::optional<int> map_message_box(float ega_x, float ega_y) {
    if (ega_x < 320.0f || ega_x > 639.0f) {
        return std::nullopt;
    }
    if (ega_y >= 139.0f && ega_y < 187.0f) return 1;
    if (ega_y >= 187.0f && ega_y < 235.0f) return 2;
    if (ega_y >= 235.0f && ega_y < 283.0f) return 3;
    if (ega_y >= 283.0f && ega_y <= 349.0f) return 4;
    return std::nullopt;
}

std::optional<std::string> parse_coordinate_input(std::string_view input) {
    std::vector<int> numbers;
    std::size_t i = 0;
    while (i < input.size()) {
        while (i < input.size() && !std::isdigit(static_cast<unsigned char>(input[i]))) {
            ++i;
        }
        if (i < input.size()) {
            int val = 0;
            while (i < input.size() && std::isdigit(static_cast<unsigned char>(input[i]))) {
                val = val * 10 + (input[i] - '0');
                ++i;
            }
            numbers.push_back(val);
        }
    }
    if (numbers.size() == 2) {
        if (numbers[0] >= 1 && numbers[0] <= 8 && numbers[1] >= 1 && numbers[1] <= 8) {
            return std::to_string(numbers[0]) + "," + std::to_string(numbers[1]);
        }
    } else if (numbers.size() == 4) {
        if (numbers[0] >= 1 && numbers[0] <= 8 && numbers[1] >= 1 && numbers[1] <= 8 &&
            numbers[2] >= 1 && numbers[2] <= 8 && numbers[3] >= 1 && numbers[3] <= 8) {
            return std::to_string(numbers[0]) + "," + std::to_string(numbers[1]) + "," +
                   std::to_string(numbers[2]) + "," + std::to_string(numbers[3]);
        }
    }
    return std::nullopt;
}

static bool has_quadrant_prefix(std::string_view cmd, int& out_qr, int& out_qc) {
    while (!cmd.empty() && std::isspace(static_cast<unsigned char>(cmd.front()))) {
        cmd.remove_prefix(1);
    }
    if (cmd.empty() || (cmd.front() != 'M' && cmd.front() != 'm')) {
        return false;
    }
    cmd.remove_prefix(1);
    while (!cmd.empty() && std::isspace(static_cast<unsigned char>(cmd.back()))) {
        cmd.remove_suffix(1);
    }
    const bool ends_with_comma = (!cmd.empty() && cmd.back() == ',');
    auto nums = extract_cmd_numbers("M " + std::string(cmd));
    if (nums.size() == 2 && ends_with_comma) {
        if (nums[0] >= 1 && nums[0] <= 8 && nums[1] >= 1 && nums[1] <= 8) {
            out_qr = nums[0];
            out_qc = nums[1];
            return true;
        }
    } else if (nums.size() >= 4) {
        if (nums[0] >= 1 && nums[0] <= 8 && nums[1] >= 1 && nums[1] <= 8) {
            out_qr = nums[0];
            out_qc = nums[1];
            return true;
        }
    }
    return false;
}

std::string format_move_command(std::string_view current_cmd, bool is_quadrant, int row, int col) {
    if (is_quadrant) {
        return "\x1bM " + std::to_string(row) + "," + std::to_string(col) + ",";
    }
    int qr = 0, qc = 0;
    if (has_quadrant_prefix(current_cmd, qr, qc)) {
        return "\x1bM " + std::to_string(qr) + "," + std::to_string(qc) + "," +
               std::to_string(row) + "," + std::to_string(col);
    }
    return "\x1bM " + std::to_string(row) + "," + std::to_string(col);
}

std::string format_coordinate_command(std::string_view current_cmd, std::string_view coords) {
    auto parsed = parse_coordinate_input(coords);
    if (!parsed) return "";
    auto coord_nums = extract_cmd_numbers("M " + std::string(coords));
    if (coord_nums.size() == 2) {
        int qr = 0, qc = 0;
        if (has_quadrant_prefix(current_cmd, qr, qc)) {
            return "\x1bM " + std::to_string(qr) + "," + std::to_string(qc) + "," + *parsed;
        }
    }
    return "\x1bM " + *parsed;
}

static bool has_quadrant_prefix_move(std::string_view text, int& out_qr, int& out_qc) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    const bool ends_with_comma = (!text.empty() && text.back() == ',');
    std::vector<int> nums;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && !std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
        if (i < text.size()) {
            int val = 0;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
                val = val * 10 + (text[i] - '0');
                ++i;
            }
            nums.push_back(val);
        }
    }
    if (nums.size() == 2 && ends_with_comma) {
        if (nums[0] >= 1 && nums[0] <= 8 && nums[1] >= 1 && nums[1] <= 8) {
            out_qr = nums[0];
            out_qc = nums[1];
            return true;
        }
    } else if (nums.size() >= 4) {
        if (nums[0] >= 1 && nums[0] <= 8 && nums[1] >= 1 && nums[1] <= 8) {
            out_qr = nums[0];
            out_qc = nums[1];
            return true;
        }
    }
    return false;
}

std::string format_move_action(bool at_move_prompt, std::string_view prompt_text,
                               bool is_quadrant, int row, int col, bool double_click,
                               std::pair<int, int> current_sector) {
    if (at_move_prompt) {
        if (is_quadrant) {
            if (double_click) {
                return "\x1bM\r" + std::to_string(row) + "," + std::to_string(col) + "," +
                       std::to_string(current_sector.first) + "," + std::to_string(current_sector.second) + "\r";
            } else {
                return "\x1bM\r" + std::to_string(row) + "," + std::to_string(col) + ",";
            }
        } else {
            // Sector click while at move prompt
            int qr = 0, qc = 0;
            if (has_quadrant_prefix_move(prompt_text, qr, qc)) {
                if (!prompt_text.empty() && prompt_text.back() == ',') {
                    // Quadrant prefix waiting for sector: e.g. "6,2,"
                    if (double_click) {
                        return std::to_string(row) + "," + std::to_string(col) + "\r";
                    } else {
                        return std::to_string(row) + "," + std::to_string(col);
                    }
                } else {
                    // Already had full coordinates: replace sector
                    if (double_click) {
                        return "\x1bM\r" + std::to_string(qr) + "," + std::to_string(qc) + "," +
                               std::to_string(row) + "," + std::to_string(col) + "\r";
                    } else {
                        return "\x1bM\r" + std::to_string(qr) + "," + std::to_string(qc) + "," +
                               std::to_string(row) + "," + std::to_string(col);
                    }
                }
            } else if (prompt_text.empty()) {
                if (double_click) {
                    return std::to_string(row) + "," + std::to_string(col) + "\r";
                } else {
                    return std::to_string(row) + "," + std::to_string(col);
                }
            } else {
                if (double_click) {
                    return "\x1bM\r" + std::to_string(row) + "," + std::to_string(col) + "\r";
                } else {
                    return "\x1bM\r" + std::to_string(row) + "," + std::to_string(col);
                }
            }
        }
    } else {
        // At main "Command: " prompt
        if (is_quadrant) {
            if (double_click) {
                return "\x1bM\r" + std::to_string(row) + "," + std::to_string(col) + "," +
                       std::to_string(current_sector.first) + "," + std::to_string(current_sector.second) + "\r";
            } else {
                return "\x1bM\r" + std::to_string(row) + "," + std::to_string(col) + ",";
            }
        } else {
            // Sector click at Command prompt
            int qr = 0, qc = 0;
            if (has_quadrant_prefix(prompt_text, qr, qc)) {
                if (double_click) {
                    return "\x1bM\r" + std::to_string(qr) + "," + std::to_string(qc) + "," +
                           std::to_string(row) + "," + std::to_string(col) + "\r";
                } else {
                    return "\x1bM\r" + std::to_string(qr) + "," + std::to_string(qc) + "," +
                           std::to_string(row) + "," + std::to_string(col);
                }
            } else {
                if (double_click) {
                    return "\x1bM\r" + std::to_string(row) + "," + std::to_string(col) + "\r";
                } else {
                    return "\x1bM\r" + std::to_string(row) + "," + std::to_string(col);
                }
            }
        }
    }
}

std::string format_coordinate_action(bool at_move_prompt, std::string_view prompt_text,
                                     std::string_view coords) {
    auto parsed = parse_coordinate_input(coords);
    if (!parsed) return "";
    auto coord_nums = extract_cmd_numbers("M " + std::string(coords));
    if (coord_nums.size() == 2) {
        int qr = 0, qc = 0;
        if (at_move_prompt) {
            if (has_quadrant_prefix_move(prompt_text, qr, qc)) {
                if (!prompt_text.empty() && prompt_text.back() == ',') {
                    return std::string(coords);
                }
                return "\x1bM\r" + std::to_string(qr) + "," + std::to_string(qc) + "," + *parsed;
            }
        } else {
            if (has_quadrant_prefix(prompt_text, qr, qc)) {
                return "\x1bM\r" + std::to_string(qr) + "," + std::to_string(qc) + "," + *parsed;
            }
        }
    }
    return "\x1bM\r" + *parsed;
}

}  // namespace trek::mouse

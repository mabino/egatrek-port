#include "core/save_file.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace trek {

namespace {

std::string trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return std::string(s);
}

}  // namespace

bool validate_save_file(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return false;

    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }

    // Ignore trailing empty line if present
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }

    if (lines.size() != 227) return false;

    // Line 1 (index 0): skill level (1..5)
    std::string skill = trim(lines[0]);
    if (skill.empty() || !std::all_of(skill.begin(), skill.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        return false;
    }
    int s = std::stoi(skill);
    if (s < 1 || s > 5) return false;

    // Line 25 (index 24): ship name (e.g. KIRK, ENTERPRISE)
    std::string ship = trim(lines[24]);
    if (ship.empty() || ship.length() > 30) return false;

    // Lines 223..227 (indices 222..226): TRUE or FALSE
    for (int i = 222; i < 227; ++i) {
        std::string b = trim(lines[i]);
        if (b != "TRUE" && b != "FALSE") return false;
    }

    return true;
}

}  // namespace trek

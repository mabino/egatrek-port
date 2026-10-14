#pragma once

#include <utility>

namespace trek {

struct WindowGeometry {
    static constexpr int kGameWidth = 640;
    static constexpr int kGameHeight = 480;
    static constexpr float kAspectNumerator = 4.0f;
    static constexpr float kAspectDenominator = 3.0f;
    static constexpr float kAspectRatio = kAspectNumerator / kAspectDenominator;

    // Checks if the given window dimensions lead to side letterboxing (pillarboxing)
    // for a 4:3 logical game presentation.
    static bool has_side_letterboxing(int window_w, int window_h) {
        if (window_h <= 0 || window_w <= 0) return false;
        const double window_aspect = static_cast<double>(window_w) / static_cast<double>(window_h);
        constexpr double target_aspect = 4.0 / 3.0;
        // Window aspect wider than 4:3 produces black bars on left and right sides.
        return window_aspect > (target_aspect + 1e-4);
    }

    // Default window dimensions matching the game's native display dimensions.
    static constexpr std::pair<int, int> default_window_size() {
        return {kGameWidth, kGameHeight};
    }
};

}  // namespace trek

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace trek::dos {

// The PC speaker driven by PIT channel 2: a square wave at 1193182 / divisor
// Hz while port 61h bits 0 and 1 are both set. State changes are timestamped
// in emulated milliseconds so tones keep their length when rendered later.
class Speaker {
public:
    static constexpr double kPitHz = 1193181.666;

    void set(bool on, std::uint16_t divisor, double at_ms);
    bool on() const { return on_; }
    double frequency() const { return on_ && divisor_ ? kPitHz / divisor_ : 0.0; }

    // Renders mono float samples covering [rendered_until, until_ms).
    void render(std::vector<float>& out, int sample_rate, double until_ms);

private:
    struct Change {
        double at_ms;
        bool on;
        std::uint16_t divisor;
    };
    std::vector<Change> pending_;
    bool on_ = false;
    std::uint16_t divisor_ = 0;
    bool play_on_ = false;
    std::uint16_t play_divisor_ = 0;
    double rendered_ms_ = -1;
    double phase_ = 0;
};

}  // namespace trek::dos

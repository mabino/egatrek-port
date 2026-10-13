#include "dos/speaker.h"

#include <cmath>

namespace trek::dos {

void Speaker::set(bool on, std::uint16_t divisor, double at_ms) {
    if (on == on_ && divisor == divisor_) return;
    on_ = on;
    divisor_ = divisor;
    pending_.push_back({at_ms, on, divisor});
}

void Speaker::render(std::vector<float>& out, int sample_rate, double until_ms) {
    if (rendered_ms_ < 0) rendered_ms_ = pending_.empty() ? until_ms : pending_.front().at_ms;
    std::size_t next = 0;
    const double step_ms = 1000.0 / sample_rate;
    while (rendered_ms_ < until_ms) {
        while (next < pending_.size() && pending_[next].at_ms <= rendered_ms_) {
            play_on_ = pending_[next].on;
            play_divisor_ = pending_[next].divisor;
            ++next;
        }
        float s = 0.0f;
        if (play_on_ && play_divisor_ > 1) {
            const double hz = kPitHz / play_divisor_;
            if (hz < sample_rate / 2.0) {
                phase_ = std::fmod(phase_ + hz / sample_rate, 1.0);
                s = phase_ < 0.5 ? 0.15f : -0.15f;
            }
        }
        out.push_back(s);
        rendered_ms_ += step_ms;
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(next));
}

}  // namespace trek::dos

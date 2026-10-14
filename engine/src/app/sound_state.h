#pragma once

#include <algorithm>
#include <cmath>

namespace trek {

class SoundState {
public:
    static constexpr float kMinVolume = 0.0f;
    static constexpr float kMaxVolume = 1.0f;
    static constexpr float kVolumeStep = 0.1f;

    explicit SoundState(float initial_volume = 1.0f, bool initial_muted = false)
        : volume_(std::clamp(initial_volume, kMinVolume, kMaxVolume)), muted_(initial_muted) {}

    float volume() const { return volume_; }
    bool muted() const { return muted_; }

    float effective_gain() const {
        return muted_ ? 0.0f : volume_;
    }

    void set_volume(float v) {
        volume_ = std::clamp(v, kMinVolume, kMaxVolume);
    }

    void set_muted(bool m) {
        muted_ = m;
    }

    void toggle_mute() {
        muted_ = !muted_;
    }

    void increase_volume(float step = kVolumeStep) {
        volume_ = std::clamp(volume_ + step, kMinVolume, kMaxVolume);
    }

    void decrease_volume(float step = kVolumeStep) {
        volume_ = std::clamp(volume_ - step, kMinVolume, kMaxVolume);
    }

    int volume_percent() const {
        return static_cast<int>(std::round(volume_ * 100.0f));
    }

    // Calculate a safe frame delta in milliseconds given host elapsed time.
    // Clamps pauses and stalls to nominal 60 Hz frame duration to prevent
    // emulated clock leaps and audio stream desynchronization.
    static constexpr double clamp_frame_delta(double raw_delta_ms) {
        if (raw_delta_ms < 0.0) return 0.0;
        if (raw_delta_ms > 33.33) return 1000.0 / 60.0; // 16.66666667 ms
        return raw_delta_ms;
    }

    // Maximum safe audio queue depth in bytes for the given sample rate and format
    // before backlog trimming is performed to maintain real-time sync.
    static constexpr int max_queued_audio_bytes(int sample_rate, int channels, int bytes_per_sample, double max_latency_sec = 0.10) {
        return static_cast<int>(sample_rate * channels * bytes_per_sample * max_latency_sec);
    }

private:
    float volume_ = 1.0f;
    bool muted_ = false;
};

}  // namespace trek

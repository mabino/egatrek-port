#include <doctest/doctest.h>

#include "app/sound_state.h"

TEST_CASE("SoundState defaults and initial values") {
    trek::SoundState sound;
    CHECK(sound.volume() == doctest::Approx(1.0f));
    CHECK_FALSE(sound.muted());
    CHECK(sound.effective_gain() == doctest::Approx(1.0f));
    CHECK(sound.volume_percent() == 100);

    trek::SoundState custom(0.5f, true);
    CHECK(custom.volume() == doctest::Approx(0.5f));
    CHECK(custom.muted());
    CHECK(custom.effective_gain() == doctest::Approx(0.0f));
    CHECK(custom.volume_percent() == 50);
}

TEST_CASE("SoundState volume clamping and stepping") {
    trek::SoundState sound(0.95f, false);
    sound.increase_volume(0.1f);
    CHECK(sound.volume() == doctest::Approx(1.0f));
    CHECK(sound.volume_percent() == 100);

    sound.decrease_volume(0.2f);
    CHECK(sound.volume() == doctest::Approx(0.8f));
    CHECK(sound.effective_gain() == doctest::Approx(0.8f));
    CHECK(sound.volume_percent() == 80);

    sound.set_volume(-0.5f);
    CHECK(sound.volume() == doctest::Approx(0.0f));
    CHECK(sound.volume_percent() == 0);

    sound.set_volume(2.5f);
    CHECK(sound.volume() == doctest::Approx(1.0f));
}

TEST_CASE("SoundState mute toggle") {
    trek::SoundState sound(0.7f, false);
    CHECK(sound.effective_gain() == doctest::Approx(0.7f));

    sound.toggle_mute();
    CHECK(sound.muted());
    CHECK(sound.effective_gain() == doctest::Approx(0.0f));
    // Stored volume is preserved while muted
    CHECK(sound.volume() == doctest::Approx(0.7f));

    sound.toggle_mute();
    CHECK_FALSE(sound.muted());
    CHECK(sound.effective_gain() == doctest::Approx(0.7f));
}

TEST_CASE("SoundState frame delta clamping prevents audio desync from UI stalls") {
    // Normal 60 Hz frame delta (~16.6 ms) should pass through
    CHECK(trek::SoundState::clamp_frame_delta(16.6) == doctest::Approx(16.6));
    CHECK(trek::SoundState::clamp_frame_delta(25.0) == doctest::Approx(25.0));

    // Negative delta should clamp to 0.0
    CHECK(trek::SoundState::clamp_frame_delta(-5.0) == doctest::Approx(0.0));

    // Large pauses (menu clicking, dragging, modal dialogs) clamp to nominal 60 Hz frame (~16.67 ms)
    CHECK(trek::SoundState::clamp_frame_delta(500.0) == doctest::Approx(1000.0 / 60.0));
    CHECK(trek::SoundState::clamp_frame_delta(2000.0) == doctest::Approx(1000.0 / 60.0));
    CHECK(trek::SoundState::clamp_frame_delta(10000.0) == doctest::Approx(1000.0 / 60.0));
}

TEST_CASE("SoundState max queued audio bytes calculation") {
    // 44100 Hz, mono (1 channel), float (4 bytes), 100 ms latency:
    // 44100 * 1 * 4 * 0.10 = 17640 bytes
    CHECK(trek::SoundState::max_queued_audio_bytes(44100, 1, 4, 0.10) == 17640);
}

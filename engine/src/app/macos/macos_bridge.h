#pragma once

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)

struct SDL_Window;

namespace trek::macos {

struct SoundBridgeCallbacks {
    void* context = nullptr;
    void (*toggle_mute)(void* context) = nullptr;
    void (*increase_volume)(void* context) = nullptr;
    void (*decrease_volume)(void* context) = nullptr;
    void (*set_volume)(void* context, float vol) = nullptr;
    void (*set_muted)(void* context, bool mute) = nullptr;
    float (*get_volume)(void* context) = nullptr;
    bool (*is_muted)(void* context) = nullptr;
    void (*step_frame)(void* context) = nullptr;
};

bool is_fullscreen_by_default();
void set_fullscreen_by_default(bool enabled);
float get_saved_sound_volume();
bool get_saved_sound_muted();
void save_sound_preferences(float volume, bool muted);

void setup_macos_app(SDL_Window* window, const SoundBridgeCallbacks& callbacks);
void sync_macos_sound_ui(float volume, bool muted);
void show_settings_window();
void show_help_window();

}  // namespace trek::macos

#else

struct SDL_Window;

namespace trek::macos {

struct SoundBridgeCallbacks {
    void* context = nullptr;
    void (*toggle_mute)(void* context) = nullptr;
    void (*increase_volume)(void* context) = nullptr;
    void (*decrease_volume)(void* context) = nullptr;
    void (*set_volume)(void* context, float vol) = nullptr;
    void (*set_muted)(void* context, bool mute) = nullptr;
    float (*get_volume)(void* context) = nullptr;
    bool (*is_muted)(void* context) = nullptr;
    void (*step_frame)(void* context) = nullptr;
};

inline bool is_fullscreen_by_default() { return false; }
inline void set_fullscreen_by_default(bool) {}
inline float get_saved_sound_volume() { return 1.0f; }
inline bool get_saved_sound_muted() { return false; }
inline void save_sound_preferences(float, bool) {}
inline void setup_macos_app(SDL_Window*, const SoundBridgeCallbacks&) {}
inline void sync_macos_sound_ui(float, bool) {}
inline void show_settings_window() {}
inline void show_help_window() {}

}  // namespace trek::macos

#endif

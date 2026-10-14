#pragma once

#include <cstddef>

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)

struct SDL_Window;

namespace trek::macos {

struct AppBridgeCallbacks {
    void* context = nullptr;
    void (*toggle_mute)(void* context) = nullptr;
    void (*increase_volume)(void* context) = nullptr;
    void (*decrease_volume)(void* context) = nullptr;
    void (*set_volume)(void* context, float vol) = nullptr;
    void (*set_muted)(void* context, bool mute) = nullptr;
    float (*get_volume)(void* context) = nullptr;
    bool (*is_muted)(void* context) = nullptr;
    void (*step_frame)(void* context) = nullptr;

    // File menu & Save/Restore callbacks
    bool (*can_save)(void* context) = nullptr;
    bool (*has_active_save)(void* context) = nullptr;
    bool (*has_unsaved_progress)(void* context) = nullptr;
    bool (*confirm_unsaved_changes)(void* context, const char* action_message) = nullptr;
    void (*new_game)(void* context) = nullptr;
    void (*save_game)(void* context) = nullptr;
    void (*save_game_as)(void* context, const char* path) = nullptr;
    void (*open_game)(void* context, const char* path) = nullptr;
    const char* (*get_active_save_path)(void* context) = nullptr;
    const char* (*get_default_save_dir)(void* context) = nullptr;
};
using SoundBridgeCallbacks = AppBridgeCallbacks;

enum class UnsavedPromptResult {
    Save = 1,
    DontSave = 0,
    Cancel = -1
};

bool is_fullscreen_by_default();
void set_fullscreen_by_default(bool enabled);
float get_saved_sound_volume();
bool get_saved_sound_muted();
void save_sound_preferences(float volume, bool muted);

void setup_macos_app(SDL_Window* window, const AppBridgeCallbacks& callbacks);
void sync_macos_sound_ui(float volume, bool muted);
void show_settings_window();
void show_help_window();

// Native modal panels
bool prompt_restore_panel(char* out_path, std::size_t max_len, const char* default_dir);
bool prompt_save_as_panel(char* out_path, std::size_t max_len, const char* default_dir, const char* default_name);
UnsavedPromptResult prompt_unsaved_changes_alert(const char* action_message);
void show_alert(const char* title, const char* message);

}  // namespace trek::macos

#else

struct SDL_Window;

namespace trek::macos {

struct AppBridgeCallbacks {
    void* context = nullptr;
    void (*toggle_mute)(void* context) = nullptr;
    void (*increase_volume)(void* context) = nullptr;
    void (*decrease_volume)(void* context) = nullptr;
    void (*set_volume)(void* context, float vol) = nullptr;
    void (*set_muted)(void* context, bool mute) = nullptr;
    float (*get_volume)(void* context) = nullptr;
    bool (*is_muted)(void* context) = nullptr;
    void (*step_frame)(void* context) = nullptr;

    bool (*can_save)(void* context) = nullptr;
    bool (*has_active_save)(void* context) = nullptr;
    bool (*has_unsaved_progress)(void* context) = nullptr;
    bool (*confirm_unsaved_changes)(void* context, const char* action_message) = nullptr;
    void (*new_game)(void* context) = nullptr;
    void (*save_game)(void* context) = nullptr;
    void (*save_game_as)(void* context, const char* path) = nullptr;
    void (*open_game)(void* context, const char* path) = nullptr;
    const char* (*get_active_save_path)(void* context) = nullptr;
    const char* (*get_default_save_dir)(void* context) = nullptr;
};
using SoundBridgeCallbacks = AppBridgeCallbacks;

enum class UnsavedPromptResult {
    Save = 1,
    DontSave = 0,
    Cancel = -1
};

inline bool is_fullscreen_by_default() { return false; }
inline void set_fullscreen_by_default(bool) {}
inline float get_saved_sound_volume() { return 1.0f; }
inline bool get_saved_sound_muted() { return false; }
inline void save_sound_preferences(float, bool) {}
inline void setup_macos_app(SDL_Window*, const AppBridgeCallbacks&) {}
inline void sync_macos_sound_ui(float, bool) {}
inline void show_settings_window() {}
inline void show_help_window() {}

inline bool prompt_restore_panel(char*, std::size_t, const char*) { return false; }
inline bool prompt_save_as_panel(char*, std::size_t, const char*, const char*) { return false; }
inline UnsavedPromptResult prompt_unsaved_changes_alert(const char*) { return UnsavedPromptResult::DontSave; }
inline void show_alert(const char*, const char*) {}

}  // namespace trek::macos

#endif

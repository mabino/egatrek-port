// EGA Trek port: application shell. Runs the recompiled TREK.EXE on the
// emulated PC (dos::Machine), presents the EGA screen at 4:3, maps the host
// keyboard to BIOS keystrokes and plays the PC speaker through SDL audio.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include "app/host_keys.h"
#include "app/macos/macos_bridge.h"
#include "app/mouse_control.h"
#include "app/sound_state.h"
#include "app/window_geometry.h"
#include "core/data_path.h"
#include "dos/machine.h"
#include "dos/rom_font.h"
#if TREK_HAVE_GAME
#include "dos/recomp.h"
#endif

namespace fs = std::filesystem;

namespace {

constexpr int kSampleRate = 44100;
// Blocks of recompiled code per frame. The game's Delay loop is calibrated
// against the BIOS clock, so this sets CPU use, not game speed.
constexpr std::int64_t kBlocksPerFrame = 1'500'000;

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_AudioStream* audio = nullptr;
    std::unique_ptr<trek::dos::Machine> machine;
    std::vector<std::uint32_t> argb = std::vector<std::uint32_t>(trek::dos::Ega::kWidth * trek::dos::Ega::kHeight);
    std::vector<float> samples;
    trek::SoundState sound;
    std::string active_save_path;
    std::string default_save_dir;
    bool smoke = false;          // --smoke: run a few frames and exit (used by tests)
    int frames = 0;
    std::uint64_t persisted_writes = 0;
    const char* shot = nullptr;  // --screenshot FILE.bmp: save a frame after --frames N and exit
    int shot_after = 1;
    double emulated_now_ms = 0.0;
    std::uint64_t last_tick_ns = 0;
    bool quitting = false;
};

SDL_AppResult step_app_frame(App& app);

#if defined(__EMSCRIPTEN__)
// Saves live in IndexedDB, mounted at /saves. Loading it is asynchronous, so
// the machine starts once Module.trekSavesReady is set.
constexpr const char* kWebSaves = "/saves";
bool web_saves_ready() { return EM_ASM_INT({ return Module.trekSavesReady ? 1 : 0; }); }
void web_saves_mount() {
    EM_ASM({
        FS.mkdir('/saves');
        FS.mount(IDBFS, {}, '/saves');
        FS.syncfs(true, function(err) {
            if (err) console.warn('EGA Trek: could not load saves', err);
            Module.trekSavesReady = true;
        });
    });
}
void web_saves_flush() {
    EM_ASM({ FS.syncfs(false, function(err) { if (err) console.warn('EGA Trek: could not store saves', err); }); });
}
#endif

fs::path save_folder() {
#if defined(__EMSCRIPTEN__)
    return kWebSaves;
#endif
    if (char* pref = SDL_GetPrefPath("mabino", "EGA Trek")) {
        fs::path p(pref);
        SDL_free(pref);
        return p;
    }
    return fs::temp_directory_path() / "EGA Trek";
}

fs::path data_folder() {
    if (auto d = trek::DataPath::from_environment()) return d->root();
    return {};
}

void draw_no_game(App& app) {
    // Built without a recompiled TREK.EXE: say so on a plain EGA screen.
    std::fill(app.argb.begin(), app.argb.end(), trek::dos::Ega::rgb(1));
    const char* lines[] = {"EGA Trek", "", "This build contains no recompiled game.",
                           "Run scripts/recomp.zsh with TREK.EXE, then rebuild."};
    int y = 140;
    for (const char* text : lines) {
        const int x0 = (trek::dos::Ega::kWidth - static_cast<int>(std::strlen(text)) * 8) / 2;
        for (int i = 0; text[i]; ++i)
            for (int gy = 0; gy < 8; ++gy) {
                const std::uint8_t bits = trek::dos::kRomFont8x8[(text[i] & 0x7F) * 8 + gy];
                for (int gx = 0; gx < 8; ++gx)
                    if (bits & (0x80 >> gx)) app.argb[(y + gy) * trek::dos::Ega::kWidth + x0 + i * 8 + gx] = 0xFFFFFFFF;
            }
        y += 16;
    }
}

bool save_bmp(App& app, const char* path) {
    SDL_Surface* surf = SDL_CreateSurfaceFrom(trek::dos::Ega::kWidth, trek::dos::Ega::kHeight,
                                              SDL_PIXELFORMAT_ARGB8888, app.argb.data(), trek::dos::Ega::kWidth * 4);
    const bool ok = surf && SDL_SaveBMP(surf, path);
    SDL_DestroySurface(surf);
    return ok;
}

void setup_machine_hooks(App& app) {
    if (!app.machine) return;

    if (!app.active_save_path.empty()) {
        app.machine->set_active_save_path(app.active_save_path);
    }

    trek::dos::Machine::StartupSettings settings;
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    settings.skip_briefing = trek::macos::should_skip_briefing();
    settings.default_difficulty = trek::macos::get_default_difficulty();
    settings.default_password = trek::macos::get_default_password();
#endif
    app.machine->set_startup_settings(settings);

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    app.machine->set_restore_hook([&app](trek::dos::Machine& m) -> bool {
        if (m.active_save_path().has_value()) {
            return true;
        }

        char chosen[1024] = {0};
        const char* def_dir = app.default_save_dir.empty() ? nullptr : app.default_save_dir.c_str();
        if (trek::macos::prompt_restore_panel(chosen, sizeof(chosen), def_dir)) {
            app.active_save_path = chosen;
            m.set_active_save_path(chosen);
            return true;
        }
        return false;
    });

    app.machine->set_save_hook([&app](trek::dos::Machine&, const fs::path& p) {
        app.active_save_path = p.string();
    });
#endif
}

void start_game([[maybe_unused]] App& app) {
#if TREK_HAVE_GAME
    const fs::path saves = save_folder();
    app.default_save_dir = saves.string();
    std::error_code ec;
    fs::create_directories(saves, ec);
    SDL_Log("saves: %s", saves.string().c_str());
    app.machine = std::make_unique<trek::dos::Machine>(trek::recomp::program(),
                                                      trek::dos::FileSystem{data_folder(), saves});
    setup_machine_hooks(app);
#endif
}

void restart_game_new(App& app) {
    app.active_save_path.clear();
    start_game(app);
}

void restart_game_with_restore(App& app, const fs::path& save_path) {
    app.active_save_path = save_path.string();
    start_game(app);
    if (app.machine) {
        app.machine->type_string(" N\rY\r");
        app.machine->run(5'000'000, app.emulated_now_ms);
    }
}

bool save_game_synchronously(App& app, const std::string& path) {
    if (!app.machine) return false;
    for (int i = 0; i < 20 && !app.machine->is_at_command_prompt(); ++i) {
        app.machine->run(50'000, app.emulated_now_ms);
    }
    if (!app.machine->is_at_command_prompt()) return false;

    app.active_save_path = path;
    app.machine->set_active_save_path(path);
    app.machine->trigger_save();

    for (int i = 0; i < 50; ++i) {
        app.machine->run(100'000, app.emulated_now_ms);
        if (app.machine->is_at_command_prompt() && !app.machine->has_unsaved_progress()) {
            return true;
        }
    }
    return true;
}

bool confirm_unsaved_changes(App& app, const char* action_message) {
    if (!app.machine || !app.machine->has_unsaved_progress()) {
        return true;
    }
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    auto res = trek::macos::prompt_unsaved_changes_alert(action_message);
    if (res == trek::macos::UnsavedPromptResult::DontSave) {
        return true;
    }
    if (res == trek::macos::UnsavedPromptResult::Cancel) {
        return false;
    }
    if (!app.active_save_path.empty()) {
        return save_game_synchronously(app, app.active_save_path);
    } else {
        char chosen[1024] = {0};
        const char* def_dir = app.default_save_dir.empty() ? nullptr : app.default_save_dir.c_str();
        if (trek::macos::prompt_save_as_panel(chosen, sizeof(chosen), def_dir, "TREK.SAV")) {
            return save_game_synchronously(app, chosen);
        }
        return false;
    }
#else
    return true;
#endif
}

}  // namespace

SDL_AppResult SDL_AppInit(void** state, int argc, char** argv) {
    auto* app = new App;
    *state = app;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) app->smoke = true;
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) app->shot = argv[++i];
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) app->shot_after = std::atoi(argv[++i]);
    }
    SDL_SetAppMetadata("EGA Trek", "0.2.0", "com.mabino.egatrek-port");
    const bool headless = app->smoke || app->shot;
    if (headless) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    // In full screen Spaces, ensure the menu bar and window controls appear when moving mouse to top edge
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, "1", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "1", SDL_HINT_OVERRIDE);
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | (headless ? 0 : SDL_INIT_AUDIO))) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    // Default to the dimensions of the game (640x480 at 4:3), avoiding letterboxing in windowed mode.
    const int init_w = trek::WindowGeometry::kGameWidth;
    const int init_h = trek::WindowGeometry::kGameHeight;
#if defined(__EMSCRIPTEN__)
    const SDL_WindowFlags init_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;
#else
    const SDL_WindowFlags init_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
#endif
    if (!SDL_CreateWindowAndRenderer("EGA Trek", init_w, init_h, init_flags,
                                     &app->window, &app->renderer)) {
        SDL_Log("window creation failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(app->renderer, 1);
    SDL_SetRenderLogicalPresentation(app->renderer, trek::WindowGeometry::kGameWidth,
                                     trek::WindowGeometry::kGameHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetWindowAspectRatio(app->window, trek::WindowGeometry::kAspectRatio, trek::WindowGeometry::kAspectRatio);
    SDL_SetWindowMinimumSize(app->window, 320, 240);

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    if (!headless && trek::macos::is_fullscreen_by_default()) {
        SDL_SetWindowFullscreen(app->window, true);
    }
#endif

    app->texture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                     trek::dos::Ega::kWidth, trek::dos::Ega::kHeight);
    SDL_SetTextureScaleMode(app->texture, SDL_SCALEMODE_PIXELART);

    if (!headless) {
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
        app->sound.set_volume(trek::macos::get_saved_sound_volume());
        app->sound.set_muted(trek::macos::get_saved_sound_muted());
#endif
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kSampleRate};
        app->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (app->audio) {
            SDL_SetAudioStreamGain(app->audio, app->sound.effective_gain());
            SDL_ResumeAudioStreamDevice(app->audio);
        } else {
            SDL_Log("no audio: %s", SDL_GetError());
        }

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
        trek::macos::SoundBridgeCallbacks callbacks;
        callbacks.context = app;
        callbacks.toggle_mute = [](void* ctx) {
            auto* a = static_cast<App*>(ctx);
            a->sound.toggle_mute();
            if (a->audio) SDL_SetAudioStreamGain(a->audio, a->sound.effective_gain());
            trek::macos::save_sound_preferences(a->sound.volume(), a->sound.muted());
            trek::macos::sync_macos_sound_ui(a->sound.volume(), a->sound.muted());
        };
        callbacks.increase_volume = [](void* ctx) {
            auto* a = static_cast<App*>(ctx);
            a->sound.increase_volume();
            if (a->audio) SDL_SetAudioStreamGain(a->audio, a->sound.effective_gain());
            trek::macos::save_sound_preferences(a->sound.volume(), a->sound.muted());
            trek::macos::sync_macos_sound_ui(a->sound.volume(), a->sound.muted());
        };
        callbacks.decrease_volume = [](void* ctx) {
            auto* a = static_cast<App*>(ctx);
            a->sound.decrease_volume();
            if (a->audio) SDL_SetAudioStreamGain(a->audio, a->sound.effective_gain());
            trek::macos::save_sound_preferences(a->sound.volume(), a->sound.muted());
            trek::macos::sync_macos_sound_ui(a->sound.volume(), a->sound.muted());
        };
        callbacks.set_volume = [](void* ctx, float vol) {
            auto* a = static_cast<App*>(ctx);
            a->sound.set_volume(vol);
            if (a->audio) SDL_SetAudioStreamGain(a->audio, a->sound.effective_gain());
            trek::macos::save_sound_preferences(a->sound.volume(), a->sound.muted());
            trek::macos::sync_macos_sound_ui(a->sound.volume(), a->sound.muted());
        };
        callbacks.set_muted = [](void* ctx, bool mute) {
            auto* a = static_cast<App*>(ctx);
            a->sound.set_muted(mute);
            if (a->audio) SDL_SetAudioStreamGain(a->audio, a->sound.effective_gain());
            trek::macos::save_sound_preferences(a->sound.volume(), a->sound.muted());
            trek::macos::sync_macos_sound_ui(a->sound.volume(), a->sound.muted());
        };
        callbacks.get_volume = [](void* ctx) -> float {
            return static_cast<App*>(ctx)->sound.volume();
        };
        callbacks.is_muted = [](void* ctx) -> bool {
            return static_cast<App*>(ctx)->sound.muted();
        };
        callbacks.step_frame = [](void* ctx) {
            step_app_frame(*static_cast<App*>(ctx));
        };
        callbacks.can_save = [](void* ctx) -> bool {
            auto* a = static_cast<App*>(ctx);
            return a->machine && a->machine->is_at_command_prompt();
        };
        callbacks.has_active_save = [](void* ctx) -> bool {
            auto* a = static_cast<App*>(ctx);
            return !a->active_save_path.empty();
        };
        callbacks.has_unsaved_progress = [](void* ctx) -> bool {
            auto* a = static_cast<App*>(ctx);
            return a->machine && a->machine->has_unsaved_progress();
        };
        callbacks.confirm_unsaved_changes = [](void* ctx, const char* action) -> bool {
            auto* a = static_cast<App*>(ctx);
            return confirm_unsaved_changes(*a, action);
        };
        callbacks.new_game = [](void* ctx) {
            auto* a = static_cast<App*>(ctx);
            if (!confirm_unsaved_changes(*a, "Do you want to save the changes to the current game before starting a new one?")) {
                return;
            }
            restart_game_new(*a);
        };
        callbacks.save_game = [](void* ctx) {
            auto* a = static_cast<App*>(ctx);
            if (a->machine && a->machine->is_at_command_prompt()) {
                if (a->active_save_path.empty()) {
                    a->active_save_path = (save_folder() / "TREK.SAV").string();
                    a->machine->set_active_save_path(a->active_save_path);
                }
                a->machine->trigger_save();
            }
        };
        callbacks.save_game_as = [](void* ctx, const char* path) {
            auto* a = static_cast<App*>(ctx);
            if (a->machine && a->machine->is_at_command_prompt() && path) {
                a->active_save_path = path;
                a->machine->set_active_save_path(path);
                a->machine->trigger_save();
            }
        };
        callbacks.open_game = [](void* ctx, const char* path) {
            auto* a = static_cast<App*>(ctx);
            if (path) {
                restart_game_with_restore(*a, path);
            }
        };
        callbacks.get_active_save_path = [](void* ctx) -> const char* {
            auto* a = static_cast<App*>(ctx);
            return a->active_save_path.empty() ? nullptr : a->active_save_path.c_str();
        };
        callbacks.get_default_save_dir = [](void* ctx) -> const char* {
            auto* a = static_cast<App*>(ctx);
            return a->default_save_dir.empty() ? nullptr : a->default_save_dir.c_str();
        };
        callbacks.can_control = [](void* ctx) -> bool {
            auto* a = static_cast<App*>(ctx);
            return a->machine && a->machine->is_at_command_prompt();
        };
        callbacks.enter_coordinates = [](void* ctx, const char* coords) {
            auto* a = static_cast<App*>(ctx);
            if (!a->machine || !a->machine->is_at_command_prompt() || !coords) return;
            auto parsed = trek::mouse::parse_coordinate_input(coords);
            if (!parsed) return;
            std::string cmd = trek::mouse::format_coordinate_command(a->machine->current_command(), *parsed);
            if (!cmd.empty()) {
                a->machine->type_string(cmd);
            }
        };
        trek::macos::setup_macos_app(app->window, callbacks);
#endif
    }

#if defined(__EMSCRIPTEN__)
    web_saves_mount();
#endif
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* event) {
    auto* app = static_cast<App*>(state);
    if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        if (!confirm_unsaved_changes(*app, "Do you want to save the changes you made to this game before quitting?")) {
            return SDL_APP_CONTINUE;
        }
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        if (!app->machine) return SDL_APP_CONTINUE;

        // Advance past title screen on mouse click
        if (app->machine->is_at_title_screen()) {
            app->machine->key_down(0x39, ' ');
            return SDL_APP_CONTINUE;
        }

        // Handle in-game clicks at command prompt
        if (app->machine->is_at_command_prompt()) {
            float lx = 0.0f, ly = 0.0f;
            SDL_RenderCoordinatesFromWindow(app->renderer, event->button.x, event->button.y, &lx, &ly);
            if (lx >= 0.0f && lx < 640.0f && ly >= 0.0f && ly < 480.0f) {
                const float ega_x = lx;
                const float ega_y = ly * (350.0f / 480.0f);

                // 1. Acknowledge message box
                if (auto box = trek::mouse::map_message_box(ega_x, ega_y)) {
                    app->machine->type_string("\033A" + std::to_string(*box) + "\r");
                    return SDL_APP_CONTINUE;
                }

                // 2. Map movement auto-fill
                bool mouse_move_enabled = true;
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
                mouse_move_enabled = trek::macos::is_mouse_movement_enabled();
#endif
                if (mouse_move_enabled) {
                    if (auto sect = trek::mouse::map_sector_coordinates(ega_x, ega_y)) {
                        std::string cmd = trek::mouse::format_move_command(app->machine->current_command(), false, sect->first, sect->second);
                        app->machine->type_string(cmd);
                        return SDL_APP_CONTINUE;
                    }
                    if (auto quad = trek::mouse::map_quadrant_coordinates(ega_x, ega_y)) {
                        std::string cmd = trek::mouse::format_move_command(app->machine->current_command(), true, quad->first, quad->second);
                        app->machine->type_string(cmd);
                        return SDL_APP_CONTINUE;
                    }
                }
            }
        }
        return SDL_APP_CONTINUE;
    }
    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        if (app->machine) app->machine->set_shift_state(trek::host_shift_flags(event->key.mod));
        if (event->type == SDL_EVENT_KEY_DOWN) {
            // Cmd-Q / Alt-F4 quit; everything else goes to the game.
            if ((event->key.mod & SDL_KMOD_GUI) && event->key.key == SDLK_Q) {
                if (!confirm_unsaved_changes(*app, "Do you want to save the changes you made to this game before quitting?")) {
                    return SDL_APP_CONTINUE;
                }
                return SDL_APP_SUCCESS;
            }
            if ((event->key.mod & SDL_KMOD_GUI) && event->key.key == SDLK_N) {
                if (!confirm_unsaved_changes(*app, "Do you want to save the changes to the current game before starting a new one?")) {
                    return SDL_APP_CONTINUE;
                }
                restart_game_new(*app);
                return SDL_APP_CONTINUE;
            }
            if (!app->machine) return event->key.key == SDLK_ESCAPE ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
            // Ignore host Command-key shortcuts so menu shortcuts don't type into the game.
            if (event->key.mod & SDL_KMOD_GUI) return SDL_APP_CONTINUE;
            if (auto k = trek::host_key(event->key.key, event->key.mod)) app->machine->key_down(k->scan, k->ascii);
        }
    }
    return SDL_APP_CONTINUE;
}

namespace {

SDL_AppResult step_app_frame(App& app) {
#if defined(__EMSCRIPTEN__)
    if (!web_saves_ready()) return SDL_APP_CONTINUE;
#endif
    if (app.quitting) return SDL_APP_SUCCESS;
    if (app.frames++ == 0) start_game(app);
    if (app.machine) {
        const std::uint64_t current_ns = SDL_GetTicksNS();
        if (app.last_tick_ns == 0) {
            app.last_tick_ns = current_ns;
            app.emulated_now_ms = static_cast<double>(current_ns) / 1e6;
        }
        const double raw_delta_ms = static_cast<double>(current_ns - app.last_tick_ns) / 1e6;
        app.last_tick_ns = current_ns;

        // Clamp frame delta to nominal duration to prevent time runaway and audio backlog during UI stalls or menu clicks.
        const double delta_ms = trek::SoundState::clamp_frame_delta(raw_delta_ms);
        app.emulated_now_ms += delta_ms;
        const double now = app.emulated_now_ms;

        const auto st = app.machine->run(kBlocksPerFrame, now);
#if defined(__EMSCRIPTEN__)
        if (app.machine->file_writes() != app.persisted_writes) {
            app.persisted_writes = app.machine->file_writes();
            web_saves_flush();
        }
#endif
        if (st == trek::dos::Machine::State::Exited) {
            app.quitting = true;
            return SDL_APP_SUCCESS;
        }
        if (st == trek::dos::Machine::State::Faulted) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "EGA Trek", app.machine->fault().c_str(), app.window);
            return SDL_APP_FAILURE;
        }
        // Blink at the EGA's ~1.9 Hz text blink rate.
        const bool blink = (static_cast<long long>(now) / 267) % 2 == 0;
        app.machine->ega().render(app.argb, app.machine->mem() + 0xB8000, trek::dos::kRomFont8x8, blink);
        if (app.audio) {
            app.samples.clear();
            app.machine->speaker().render(app.samples, kSampleRate, now);
            if (!app.samples.empty()) {
                // Ensure queued audio backlog never exceeds 100 ms so sound stays in tight sync with the game
                const int max_queued = trek::SoundState::max_queued_audio_bytes(kSampleRate, 1, sizeof(float), 0.10);
                if (SDL_GetAudioStreamQueued(app.audio) > max_queued) {
                    SDL_ClearAudioStream(app.audio);
                }
                SDL_PutAudioStreamData(app.audio, app.samples.data(),
                                       static_cast<int>(app.samples.size() * sizeof(float)));
            }
        }
    } else {
        draw_no_game(app);
    }
    if (app.shot && app.frames >= app.shot_after) return save_bmp(app, app.shot) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    SDL_UpdateTexture(app.texture, nullptr, app.argb.data(), trek::dos::Ega::kWidth * 4);
    SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
    SDL_RenderClear(app.renderer);
    SDL_RenderTexture(app.renderer, app.texture, nullptr, nullptr);
    SDL_RenderPresent(app.renderer);
    return app.smoke && app.frames >= 3 ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

}  // namespace

SDL_AppResult SDL_AppIterate(void* state) {
    return step_app_frame(*static_cast<App*>(state));
}

void SDL_AppQuit(void* state, SDL_AppResult) {
    auto* app = static_cast<App*>(state);
    if (app->audio) SDL_DestroyAudioStream(app->audio);
    if (app->texture) SDL_DestroyTexture(app->texture);
    if (app->renderer) SDL_DestroyRenderer(app->renderer);
    if (app->window) SDL_DestroyWindow(app->window);
    delete app;
}

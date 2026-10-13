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
    bool smoke = false;          // --smoke: run a few frames and exit (used by tests)
    int frames = 0;
    std::uint64_t persisted_writes = 0;
    const char* shot = nullptr;  // --screenshot FILE.bmp: save a frame after --frames N and exit
    int shot_after = 1;
};

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
    if (!SDL_Init(SDL_INIT_VIDEO | (headless ? 0 : SDL_INIT_AUDIO))) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    // EGA's 640x350 filled a 4:3 monitor, so pixels are 1.37 times taller than wide.
    if (!SDL_CreateWindowAndRenderer("EGA Trek", 1280, 960, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE,
                                     &app->window, &app->renderer)) {
        SDL_Log("window creation failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(app->renderer, 1);
    SDL_SetRenderLogicalPresentation(app->renderer, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    app->texture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                     trek::dos::Ega::kWidth, trek::dos::Ega::kHeight);
    SDL_SetTextureScaleMode(app->texture, SDL_SCALEMODE_PIXELART);

    if (!headless) {
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kSampleRate};
        app->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (app->audio) SDL_ResumeAudioStreamDevice(app->audio);
        else SDL_Log("no audio: %s", SDL_GetError());
    }

#if defined(__EMSCRIPTEN__)
    web_saves_mount();
#endif
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* event) {
    auto* app = static_cast<App*>(state);
    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        if (app->machine) app->machine->set_shift_state(trek::host_shift_flags(event->key.mod));
        if (event->type == SDL_EVENT_KEY_DOWN) {
            // Cmd-Q / Alt-F4 quit; everything else goes to the game.
            if ((event->key.mod & SDL_KMOD_GUI) && event->key.key == SDLK_Q) return SDL_APP_SUCCESS;
            if (!app->machine) return event->key.key == SDLK_ESCAPE ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
            if (auto k = trek::host_key(event->key.key, event->key.mod)) app->machine->key_down(k->scan, k->ascii);
        }
    }
    return SDL_APP_CONTINUE;
}

void start_game([[maybe_unused]] App& app) {
#if TREK_HAVE_GAME
    const fs::path saves = save_folder();
    std::error_code ec;
    fs::create_directories(saves, ec);
    SDL_Log("saves: %s", saves.string().c_str());
    app.machine = std::make_unique<trek::dos::Machine>(trek::recomp::program(),
                                                      trek::dos::FileSystem{data_folder(), saves});
#endif
}

SDL_AppResult SDL_AppIterate(void* state) {
    auto* app = static_cast<App*>(state);
#if defined(__EMSCRIPTEN__)
    if (!web_saves_ready()) return SDL_APP_CONTINUE;
#endif
    if (app->frames++ == 0) start_game(*app);
    if (app->machine) {
        const double now = static_cast<double>(SDL_GetTicksNS()) / 1e6;
        const auto st = app->machine->run(kBlocksPerFrame, now);
#if defined(__EMSCRIPTEN__)
        if (app->machine->file_writes() != app->persisted_writes) {
            app->persisted_writes = app->machine->file_writes();
            web_saves_flush();
        }
#endif
        if (st == trek::dos::Machine::State::Exited) return SDL_APP_SUCCESS;
        if (st == trek::dos::Machine::State::Faulted) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "EGA Trek", app->machine->fault().c_str(), app->window);
            return SDL_APP_FAILURE;
        }
        // Blink at the EGA's ~1.9 Hz text blink rate.
        const bool blink = (static_cast<long long>(now) / 267) % 2 == 0;
        app->machine->ega().render(app->argb, app->machine->mem() + 0xB8000, trek::dos::kRomFont8x8, blink);
        if (app->audio) {
            app->samples.clear();
            app->machine->speaker().render(app->samples, kSampleRate, now);
            if (!app->samples.empty())
                SDL_PutAudioStreamData(app->audio, app->samples.data(),
                                       static_cast<int>(app->samples.size() * sizeof(float)));
        }
    } else {
        draw_no_game(*app);
    }
    if (app->shot && app->frames >= app->shot_after) return save_bmp(*app, app->shot) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    SDL_UpdateTexture(app->texture, nullptr, app->argb.data(), trek::dos::Ega::kWidth * 4);
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    SDL_RenderTexture(app->renderer, app->texture, nullptr, nullptr);
    SDL_RenderPresent(app->renderer);
    return app->smoke && app->frames >= 3 ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* state, SDL_AppResult) {
    auto* app = static_cast<App*>(state);
    if (app->audio) SDL_DestroyAudioStream(app->audio);
    if (app->texture) SDL_DestroyTexture(app->texture);
    if (app->renderer) SDL_DestroyRenderer(app->renderer);
    if (app->window) SDL_DestroyWindow(app->window);
    delete app;
}

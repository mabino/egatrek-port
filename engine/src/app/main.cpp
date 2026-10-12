// EGA Trek port: application shell. Opens the window, loads TREK.EXE from the
// user's copy of the game and presents the EGA framebuffer; game systems are
// wired in as they are decompiled.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "core/byte_reader.h"
#include "core/data_path.h"
#include "core/mz_image.h"
#include "gfx/ega_surface.h"
#include "gfx/stroke_font.h"

namespace {

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    std::optional<trek::DataPath> data;
    std::optional<trek::StrokeFont> font;
    trek::EgaSurface screen;
    std::vector<std::uint32_t> argb = std::vector<std::uint32_t>(trek::EgaSurface::kWidth * trek::EgaSurface::kHeight);
    bool smoke = false;        // --smoke: render one frame and exit (used by tests)
    const char* shot = nullptr;  // --screenshot FILE.bmp: save the EGA frame and exit
};

void draw_placeholder(App& app) {
    auto& s = app.screen;
    s.clear(0);
    s.rectangle(0, 0, trek::EgaSurface::kWidth - 1, trek::EgaSurface::kHeight - 1, 9);
    if (app.font) {
        const char* title = "EGA Trek";
        const int w = app.font->text_width(title, 3, 1);
        app.font->draw(s, (trek::EgaSurface::kWidth - w) / 2, 110, title, 14, 3, 1);
        const char* sub = "port in progress";
        app.font->draw(s, (trek::EgaSurface::kWidth - app.font->text_width(sub)) / 2, 220, sub, 11);
    } else {
        s.bar(220, 160, 420, 190, 4);  // no TREK.EXE: red bar, details in the log
    }
}

bool save_bmp(const App& app, const char* path) {
    SDL_Surface* surf = SDL_CreateSurfaceFrom(trek::EgaSurface::kWidth, trek::EgaSurface::kHeight,
                                              SDL_PIXELFORMAT_ARGB8888, const_cast<std::uint32_t*>(app.argb.data()),
                                              trek::EgaSurface::kWidth * 4);
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
    }
    SDL_SetAppMetadata("EGA Trek", "0.1.0", "com.mabino.egatrek-port");
    if (app->smoke || app->shot) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    // EGA's 640x350 fills a 4:3 monitor, so pixels are 1.37 times taller than wide.
    if (!SDL_CreateWindowAndRenderer("EGA Trek", 1280, 960, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE,
                                     &app->window, &app->renderer)) {
        SDL_Log("window creation failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderLogicalPresentation(app->renderer, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    app->texture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                     trek::EgaSurface::kWidth, trek::EgaSurface::kHeight);
    SDL_SetTextureScaleMode(app->texture, SDL_SCALEMODE_NEAREST);

    app->data = trek::DataPath::from_environment();
    if (app->data) {
        SDL_Log("game data: %s", app->data->root().string().c_str());
        try {
            auto exe = trek::MzImage::load(*app->data->resolve("TREK.EXE"));
            app->font = trek::StrokeFont::find_in(exe.image());
        } catch (const trek::ReadError& e) {
            SDL_Log("cannot read TREK.EXE: %s", e.what());
        }
    } else {
        SDL_Log("game data not found; set TREK_DATA to the folder holding TREK.EXE");
    }
    draw_placeholder(*app);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void*, SDL_Event* event) {
    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* state) {
    auto* app = static_cast<App*>(state);
    app->screen.to_argb(app->argb, trek::EgaSurface::default_palette());
    if (app->shot) return save_bmp(*app, app->shot) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    SDL_UpdateTexture(app->texture, nullptr, app->argb.data(), trek::EgaSurface::kWidth * 4);
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    SDL_RenderTexture(app->renderer, app->texture, nullptr, nullptr);
    SDL_RenderPresent(app->renderer);
    return app->smoke ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* state, SDL_AppResult) {
    auto* app = static_cast<App*>(state);
    if (app->texture) SDL_DestroyTexture(app->texture);
    if (app->renderer) SDL_DestroyRenderer(app->renderer);
    if (app->window) SDL_DestroyWindow(app->window);
    delete app;
}

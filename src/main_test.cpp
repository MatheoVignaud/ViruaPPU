#include <ModesImpl.hpp>
#include <SDL3/SDL.h>
#include <VirtuaPPU.hpp>
#include <algorithm>
#include <array>
#include <assets.h>
#include <cmath>
#include <cstring>

static constexpr int NUM_CLOUDS = 6;
static constexpr int NUM_BIRDS = 5;
static constexpr double PI = 3.14159265358979;

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    // Configure registers
    global_Registers.mode = 0;
    global_Registers.frame_width = 840;

    // Try normal init, fallback to dummy/software in headless envs
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        const char *err1 = SDL_GetError();
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            SDL_Log("Unable to initialize SDL (normal='%s', dummy='%s')", err1, SDL_GetError());
            return 0;
        }
    }

    SDL_Window *window = SDL_CreateWindow("VirtuaPPU - Sunny Day Demo",
                                          global_Registers.frame_width * 3, 360 * 3, 0);
    if (!window)
    {
        SDL_Log("Could not create window: %s", SDL_GetError());
        SDL_Quit();
        return 0;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer)
    {
        SDL_Log("Could not create renderer: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

    SDL_SetRenderScale(renderer, 2.0f, 2.0f);

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             global_Registers.frame_width, 360);
    if (!texture)
    {
        SDL_Log("Could not create texture: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

    // Set nearest neighbor filtering for pixel-perfect rendering
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    bool quit = false;
    SDL_Event ev;
    uint32_t frame_counter = 0;
    uint32_t last_fps_ts = SDL_GetTicks();
    double current_fps = 0.0;

    while (!quit)
    {
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT)
                quit = true;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE)
                quit = true;
        }

        // Time-based animation
        const double t = SDL_GetTicks() * 0.001;

        // Render frame
        RenderFrame();
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

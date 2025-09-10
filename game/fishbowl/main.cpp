// ===== quick probes (adjust and rebuild) =====
#define FISHBOWL_PROBE_SDL_ONLY 0       // set to 1 to bypass bgfx and show plain SDL window
#define FISHBOWL_FORCE_GL_ON_MAC 0      // set to 1 to force OpenGL on macOS instead of Metal
// =============================================

#include <cstdio>
#include <cstdlib>
#include <SDL.h>
#include <SDL_syswm.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include "tracy/Tracy.hpp" // requires external/tracy set up

//--------------------------------------------------------------------------------------
// Get native window handle for bgfx::Init.platformData.{nwh, ndt}
//--------------------------------------------------------------------------------------
static void* getNativeWindowHandle(SDL_Window* window, void** outNdt)
{
    *outNdt = nullptr;

    SDL_SysWMinfo wmi{};
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(window, &wmi)) {
        std::fprintf(stderr, "SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
        return nullptr;
    }

#if defined(_WIN32)
    return (void*)wmi.info.win.window;
#elif defined(__APPLE__)
    return (void*)wmi.info.cocoa.window;
#elif defined(__linux__)
    if (wmi.subsystem == SDL_SYSWM_X11) {
        *outNdt = (void*)wmi.info.x11.display;
        return (void*)wmi.info.x11.window;
    } else if (wmi.subsystem == SDL_SYSWM_WAYLAND) {
        *outNdt = (void*)wmi.info.wl.display;
        return (void*)wmi.info.wl.surface;
    }
    return nullptr;
#else
    return nullptr;
#endif
}

int main(int /*argc*/, char** /*argv*/)
{
    ZoneScoped; // Tracy profiling scope

    // --- macOS: make sure app is foreground & plays nice with Spaces ---
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "0");
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "1");

    // Verbose SDL logs for startup diagnostics
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);

    std::puts("Starting Voxel Fish...");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    std::puts("SDL_Init ok.");

    // Create SDL window
    int winW = 1600, winH = 900;
    SDL_Window* window = SDL_CreateWindow(
        "Voxel Fish (Day 1)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        winW, winH,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Force window visible & foreground
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    SDL_SetWindowAlwaysOnTop(window, SDL_TRUE);
    SDL_SetWindowAlwaysOnTop(window, SDL_FALSE);

    // Log geometry
    int x, y; SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &winW, &winH);
    std::printf("Window pos=(%d,%d) size=(%dx%d)\n", x, y, winW, winH);

#if FISHBOWL_PROBE_SDL_ONLY
    // ---- SDL-only probe: NO bgfx, just show a window so we can confirm visibility ----
    {
        bool running = true;
        Uint32 start = SDL_GetTicks();
        SDL_Event e{};
        while (running) {
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
            }
            if (SDL_GetTicks() - start > 10000) running = false; // auto-exit after ~10s
            SDL_Delay(16);
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
#endif

    // ---- bgfx path ----
    void* ndt = nullptr;
    void* nwh = getNativeWindowHandle(window, &ndt);
    if (!nwh) {
        std::fprintf(stderr, "Native window handle is null. Exiting.\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bgfx::Init init{};
#if defined(_WIN32)
    init.type = bgfx::RendererType::Direct3D11; // safe on Windows
#elif defined(__APPLE__)
  #if FISHBOWL_FORCE_GL_ON_MAC
    init.type = bgfx::RendererType::OpenGL;     // diagnostic: force GL on mac (instead of Metal)
  #else
    init.type = bgfx::RendererType::Count;      // auto-pick (Metal on mac)
  #endif
#else
    init.type = bgfx::RendererType::Count;      // auto elsewhere
#endif
    init.platformData.nwh = nwh;
    init.platformData.ndt = ndt;
    init.resolution.width  = (uint32_t)winW;
    init.resolution.height = (uint32_t)winH;
    init.resolution.reset  = BGFX_RESET_VSYNC;

    if (!bgfx::init(init)) {
        std::fprintf(stderr, "bgfx::init failed.\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // IMPORTANT: set/reset AFTER init (and on every resize)
    bgfx::reset((uint32_t)winW, (uint32_t)winH, init.resolution.reset);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x203040ff, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, (uint16_t)winW, (uint16_t)winH);
    bgfx::touch(0);

    bool running = true;
    SDL_Event e{};
    while (running) {
        ZoneScopedN("MainLoop");

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    SDL_GetWindowSize(window, &winW, &winH);
                    bgfx::reset((uint32_t)winW, (uint32_t)winH, init.resolution.reset);
                    bgfx::setViewRect(0, 0, 0, (uint16_t)winW, (uint16_t)winH);
                }
            }
        }

        bgfx::touch(0);   // ensure the view clears even with no draws
        FrameMark;        // Tracy frame boundary
        bgfx::frame();    // present
    }

    bgfx::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

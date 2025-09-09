#include <cstdio>
#include <SDL.h>
#include <SDL_syswm.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include "tracy/Tracy.hpp"  // Tracy single-header (external/tracy/public/tracy/Tracy.hpp)

//--------------------------------------------------------------------------------------
// Get native window handle for bgfx::Init.platformData.{nwh,ndt}
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
    // HWND
    return (void*)wmi.info.win.window;
#elif defined(__APPLE__)
    // NSWindow*
    return (void*)wmi.info.cocoa.window;
#elif defined(__linux__)
    // X11 Display/Window or Wayland display/surface
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
    ZoneScoped; // Tracy: profile scope for main()

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Create window
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

    // Fetch native handles for bgfx
    void* ndt = nullptr;
    void* nwh = getNativeWindowHandle(window, &ndt);
    if (!nwh) {
        std::fprintf(stderr, "Native window handle is null. Exiting.\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Initialize bgfx
    bgfx::Init init{};
#if defined(_WIN32)
    init.type = bgfx::RendererType::Direct3D11; // Force a safe backend on Windows first
#else
    init.type = bgfx::RendererType::Count;      // Auto-pick (Metal on macOS, etc.)
#endif
    init.platformData.nwh = nwh;  // main window handle
    init.platformData.ndt = ndt;  // display/connection (X11/Wayland), null on Win/mac
    init.resolution.width  = (uint32_t)winW;
    init.resolution.height = (uint32_t)winH;
    init.resolution.reset  = BGFX_RESET_VSYNC;  // add BGFX_RESET_HIDPI later if desired

    if (!bgfx::init(init)) {
        std::fprintf(stderr, "bgfx::init failed. "
                             "If on Windows, ensure GPU drivers are up to date. "
                             "Renderer tried: %s\n",
#if defined(_WIN32)
                      "Direct3D11"
#else
                      "Auto"
#endif
        );
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // View 0 setup
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x203040ff, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, (uint16_t)winW, (uint16_t)winH);

    bool running = true;
    SDL_Event e{};
    while (running) {
        ZoneScopedN("MainLoop"); // Tracy

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    int w = 0, h = 0;
                    SDL_GetWindowSize(window, &w, &h);
                    winW = w; winH = h;
                    bgfx::reset((uint32_t)w, (uint32_t)h, init.resolution.reset);
                    bgfx::setViewRect(0, 0, 0, (uint16_t)w, (uint16_t)h);
                }
            }
        }

        // Ensure view 0 is touched so it clears even with no draws
        bgfx::touch(0);

        FrameMark;    // Tracy frame boundary
        bgfx::frame(); // present
    }

    bgfx::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

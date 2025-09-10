// ===== quick probes (adjust and rebuild) =====
#define FISHBOWL_PROBE_SDL_ONLY 0       // 1 => bypass bgfx and just show an SDL window
#define FISHBOWL_FORCE_GL_ON_MAC 0      // 1 => force OpenGL on macOS instead of Metal
// =============================================

#include <cstdio>
#include <cstdlib>

#include <SDL.h>
#include <SDL_syswm.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#ifdef __APPLE__
  #include <SDL_metal.h>   // gives us SDL_MetalCreateView / GetLayer
#endif

#include "tracy/Tracy.hpp" // comment out if Tracy not set up

//--------------------------------------------------------------------------------------
// Get native window handle for bgfx::Init.platformData.{nwh, ndt} (non-Metal fallback)
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
    return (void*)wmi.info.cocoa.window; // only used if falling back to GL
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

    // ---- SDL hints (before SDL_Init) ----
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "0");            // prefer foreground app
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "1");   // better Spaces behavior
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");            // quiet noisy logs
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
    std::puts("Starting Voxel Fish...");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    std::puts("SDL_Init ok.");

    // ---- Create SDL window ----
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

    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    SDL_PumpEvents(); // flush Cocoa “show” events

    int x, y; SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &winW, &winH);
    std::printf("Window pos=(%d,%d) size=(%dx%d)\n", x, y, winW, winH);

#if FISHBOWL_PROBE_SDL_ONLY
    // ---- SDL-only probe: NO bgfx ----
    {
        bool running = true;
        Uint32 start = SDL_GetTicks();
        SDL_Event e{};
        while (running) {
            if (!SDL_WaitEventTimeout(&e, 8)) SDL_PumpEvents();
            do {
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
            } while (SDL_PollEvent(&e));
            SDL_Delay(4);
            if (SDL_GetTicks() - start > 10000) running = false; // auto-exit after ~10s
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
#endif

    // ---- bgfx init path ----
    void* ndt = nullptr;
    void* nwh = getNativeWindowHandle(window, &ndt);

#ifdef __APPLE__
  #if FISHBOWL_FORCE_GL_ON_MAC
    bool useMetal = false;
  #else
    bool useMetal = true;
  #endif
#else
    bool useMetal = false;
#endif

#ifdef __APPLE__
    SDL_MetalView metalView = nullptr;
    void* metalLayer = nullptr; // keep opaque

    if (useMetal) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
        metalView = SDL_Metal_CreateView(window);
        if (!metalView) {
            std::fprintf(stderr, "SDL_Metal_CreateView failed: %s\n", SDL_GetError());
            useMetal = false;
        } else {
            metalLayer = SDL_Metal_GetLayer(metalView);
            if (!metalLayer) {
                std::fprintf(stderr, "SDL_Metal_GetLayer returned null.\n");
                useMetal = false;
                SDL_Metal_DestroyView(metalView);
                metalView = nullptr;
            }
        }
    }
#endif

#if BX_PLATFORM_OSX
    // Force single-threaded mode once on macOS
    bgfx::renderFrame();
#endif

    bgfx::Init init{};
#if defined(_WIN32)
    init.type = bgfx::RendererType::Direct3D11;
#elif defined(__APPLE__)
    init.type = useMetal ? bgfx::RendererType::Count
                         : bgfx::RendererType::OpenGL;
#else
    init.type = bgfx::RendererType::Count;
#endif

#ifdef __APPLE__
    if (useMetal) {
        init.platformData.nwh = metalLayer; // CAMetalLayer* as void*
        init.platformData.ndt = nullptr;
    } else {
        init.platformData.nwh = nwh;
        init.platformData.ndt = ndt;
    }
#else
    init.platformData.nwh = nwh;
    init.platformData.ndt = ndt;
#endif

    init.resolution.width  = (uint32_t)winW;
    init.resolution.height = (uint32_t)winH;
    init.resolution.reset  = BGFX_RESET_VSYNC;

    if (!bgfx::init(init)) {
        std::fprintf(stderr, "bgfx::init failed.\n");
#ifdef __APPLE__
        if (metalView) SDL_Metal_DestroyView(metalView);
#endif
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bgfx::reset((uint32_t)winW, (uint32_t)winH, init.resolution.reset);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x203040ff, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, (uint16_t)winW, (uint16_t)winH);
    bgfx::touch(0);

    // Optional debug text
    // bgfx::setDebug(BGFX_DEBUG_TEXT);

    // ---- Adaptive main loop ----
    bool running = true;
    SDL_Event e{};
    bool dirty = true;                               // redraw requested
    bool focused = true;                             // window focus state
    uint64_t lastCounter = SDL_GetPerformanceCounter();
    const double freq   = (double)SDL_GetPerformanceFrequency();
    const double target = 1.0 / 60.0;                // 60 FPS cap
    const uint32_t idlePollMs   = 100;               // ~10 FPS polling when idle
    const uint32_t activePollMs = 8;                 // tight poll when active
    const uint32_t unfocusedMs  = 33;                // ~30 FPS polling when not focused

    while (running) {
        ZoneScopedN("MainLoop");

        // Pick a poll timeout based on state
        uint32_t waitMs = focused ? (dirty ? activePollMs : idlePollMs) : unfocusedMs;

        bool gotEvent = SDL_WaitEventTimeout(&e, waitMs) == 1;
        if (!gotEvent) {
            SDL_PumpEvents();
        } else {
            do {
                switch (e.type) {
                  case SDL_QUIT: running = false; break;
                  case SDL_KEYDOWN:
                    if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                    dirty = true;
                    break;
                  case SDL_MOUSEBUTTONDOWN:
                  case SDL_MOUSEBUTTONUP:
                  case SDL_MOUSEMOTION:
                  case SDL_MOUSEWHEEL:
                    dirty = true;
                    break;
                  case SDL_WINDOWEVENT:
                    if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                        e.window.event == SDL_WINDOWEVENT_RESIZED) {
                        SDL_GetWindowSize(window, &winW, &winH);
                        bgfx::reset((uint32_t)winW, (uint32_t)winH, init.resolution.reset);
                        bgfx::setViewRect(0, 0, 0, (uint16_t)winW, (uint16_t)winH);
                        dirty = true;
                    } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                        focused = true; dirty = true;
                    } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                        focused = false;
                    }
                    break;
                  default: break;
                }
            } while (SDL_PollEvent(&e));
        }

        if (!focused) {
            SDL_Delay(1);
            bgfx::touch(0);
            FrameMark;
            bgfx::frame();
            continue;
        }

        double dt = (SDL_GetPerformanceCounter() - lastCounter) / freq;
        if (!dirty && dt < target) {
            SDL_Delay(1);
            continue;
        }
        if (dt < target) {
            uint32_t sleepMs = (uint32_t)((target - dt) * 1000.0);
            if (sleepMs > 0) SDL_Delay(sleepMs);
        }
        lastCounter = SDL_GetPerformanceCounter();

        // --------- Render frame ----------
        bgfx::touch(0);
        // Optional debug text:
        // bgfx::dbgTextClear();
        // bgfx::dbgTextPrintf(1, 1, 0x0F, "Hello Voxel Fish 🐟");

        FrameMark;
        bgfx::frame();

        dirty = false;
    }

    bgfx::shutdown();
#ifdef __APPLE__
    if (metalView) SDL_Metal_DestroyView(metalView);
#endif
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

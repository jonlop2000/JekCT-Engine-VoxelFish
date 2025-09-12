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

#include "Camera.h"          // from engine_core PUBLIC include
#include "vs_simple.bin.h"   // generated at build time
#include "fs_simple.bin.h"

struct PosNormalVertex {
    float x, y, z;
    float nx, ny, nz;
    static bgfx::VertexLayout layout() {
        bgfx::VertexLayout l;
        l.begin()
         .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
         .add(bgfx::Attrib::Normal,   3, bgfx::AttribType::Float)
         .end();
        return l;
    }
};

static const PosNormalVertex s_cubeVerts[] = {
    {-1,-1, 1, 0,0,1}, { 1,-1, 1, 0,0,1}, { 1, 1, 1, 0,0,1}, {-1, 1, 1, 0,0,1},
    {-1,-1,-1, 0,0,-1}, {-1, 1,-1,0,0,-1}, { 1, 1,-1,0,0,-1}, { 1,-1,-1,0,0,-1},
    {-1, 1,-1, 0,1,0}, {-1, 1, 1,0,1,0}, { 1, 1, 1,0,1,0}, { 1, 1,-1,0,1,0},
    {-1,-1,-1, 0,-1,0}, { 1,-1,-1,0,-1,0}, { 1,-1, 1,0,-1,0}, {-1,-1, 1,0,-1,0},
    { 1,-1,-1, 1,0,0}, { 1, 1,-1,1,0,0}, { 1, 1, 1,1,0,0}, { 1,-1, 1,1,0,0},
    {-1,-1,-1,-1,0,0}, {-1,-1, 1,-1,0,0}, {-1, 1, 1,-1,0,0}, {-1, 1,-1,-1,0,0},
};
static const uint16_t s_cubeIndices[] = {
    0,1,2, 0,2,3,  4,5,6, 4,6,7,  8,9,10, 8,10,11,
    12,13,14, 12,14,15,  16,17,18, 16,18,19,  20,21,22, 20,22,23
};


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

    // ---- View IDs ----
    const bgfx::ViewId kMain = 0;

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
    bgfx::setViewRect(kMain, 0, 0, (uint16_t)winW, (uint16_t)winH);
    bgfx::setViewClear(kMain, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x203040ff, 1.0f, 0);
    bgfx::touch(kMain);

    // Optional but helpful: show stats/debug so you *see* BGFX is alive.
    bgfx::setDebug(BGFX_DEBUG_TEXT | BGFX_DEBUG_STATS);

    // Create GPU resources
    bgfx::VertexLayout vlayout = PosNormalVertex::layout();
    bgfx::VertexBufferHandle vbh =
        bgfx::createVertexBuffer(bgfx::makeRef(s_cubeVerts, sizeof(s_cubeVerts)), vlayout);
    bgfx::IndexBufferHandle ibh =
        bgfx::createIndexBuffer(bgfx::makeRef(s_cubeIndices, sizeof(s_cubeIndices)));

    auto loadEmbeddedShader = [](const uint8_t* data, uint32_t size) {
        const bgfx::Memory* mem = bgfx::copy(data, size);
        return bgfx::createShader(mem);
    };

    bgfx::ShaderHandle vsh = loadEmbeddedShader(vs_simple, sizeof(vs_simple));
    bgfx::ShaderHandle fsh = loadEmbeddedShader(fs_simple, sizeof(fs_simple));
    bgfx::ProgramHandle prog = bgfx::createProgram(vsh, fsh, true);

    if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh) || !bgfx::isValid(prog)) {
        std::fprintf(stderr, "Shader load/create failed.\n");
    }

    bgfx::UniformHandle u_lightDir = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);


    // ---- Camera setup ----
    FlyCamera camera;
    camera.pos[0] = 0; camera.pos[1] = 0; camera.pos[2] = -5;
    
    // ---- Input state ----
    bool keys[256] = {false};
    bool mouseCaptured = false;
    int lastMouseX = 0, lastMouseY = 0;

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
                    if (e.key.keysym.scancode < 256) keys[e.key.keysym.scancode] = true;
                    dirty = true;
                    break;
                  case SDL_KEYUP:
                    if (e.key.keysym.scancode < 256) keys[e.key.keysym.scancode] = false;
                    break;
                  case SDL_MOUSEBUTTONDOWN:
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        mouseCaptured = !mouseCaptured;
                        SDL_SetRelativeMouseMode(mouseCaptured ? SDL_TRUE : SDL_FALSE);
                        dirty = true;
                    }
                    break;
                  case SDL_MOUSEMOTION:
                    if (mouseCaptured) {
                        camera.yaw   -= e.motion.xrel * 0.005f;
                        camera.pitch -= e.motion.yrel * 0.005f;
                        camera.pitch = std::max(-1.57f, std::min(1.57f, camera.pitch));
                        dirty = true;
                    }
                    break;
                  case SDL_WINDOWEVENT:
                    if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                        e.window.event == SDL_WINDOWEVENT_RESIZED) {
                        SDL_GetWindowSize(window, &winW, &winH);
                        bgfx::reset((uint32_t)winW, (uint32_t)winH, init.resolution.reset);
                        bgfx::setViewRect(kMain, 0, 0, (uint16_t)winW, (uint16_t)winH);
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
            bgfx::touch(kMain);
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

        // ---- Camera movement (WASD) ----
        const float moveSpeed = 5.0f * (float)dt;
        const float cy = std::cos(camera.yaw), sy = std::sin(camera.yaw);
        const float cp = std::cos(camera.pitch), sp = std::sin(camera.pitch);
        
        if (keys[SDL_SCANCODE_W]) {
            camera.pos[0] += cy * cp * moveSpeed;
            camera.pos[1] += sp * moveSpeed;
            camera.pos[2] += sy * cp * moveSpeed;
        }
        if (keys[SDL_SCANCODE_S]) {
            camera.pos[0] -= cy * cp * moveSpeed;
            camera.pos[1] -= sp * moveSpeed;
            camera.pos[2] -= sy * cp * moveSpeed;
        }
        if (keys[SDL_SCANCODE_A]) {
            camera.pos[0] -= sy * moveSpeed;
            camera.pos[2] += cy * moveSpeed;
        }
        if (keys[SDL_SCANCODE_D]) {
            camera.pos[0] += sy * moveSpeed;
            camera.pos[2] -= cy * moveSpeed;
        }

        // ---- Set up views ----
        float view[16], proj[16];
        camera.buildViewProj(view, proj, (float)winW / winH, bgfx::getCaps()->homogeneousDepth);
        
        bgfx::setViewTransform(kMain, view, proj);
        bgfx::touch(kMain); // keeps the clear active

        // ---- Render cube ----
        float mtx[16];
        bx::mtxIdentity(mtx);
        bgfx::setTransform(mtx);
        
        // Start permissive: turn off depth & culling until you see pixels.
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                     /*| BGFX_STATE_DEPTH_TEST_LESS*/
                     /*| BGFX_STATE_CULL_CW*/
                     | BGFX_STATE_MSAA);
        
        // Set normalized light direction
        float lightDir[4] = { 0.6f, -0.8f, 0.0f, 0.0f };
        float len = std::sqrt(lightDir[0]*lightDir[0] + lightDir[1]*lightDir[1] + lightDir[2]*lightDir[2]);
        lightDir[0] /= len; lightDir[1] /= len; lightDir[2] /= len;
        bgfx::setUniform(u_lightDir, lightDir);
        
        bgfx::setVertexBuffer(0, vbh);
        bgfx::setIndexBuffer(ibh);
        bgfx::submit(kMain, prog);

        FrameMark;
        bgfx::frame();

        dirty = false;
    }

    // Cleanup GPU resources
    bgfx::destroy(u_lightDir);
    bgfx::destroy(prog);
    bgfx::destroy(ibh);
    bgfx::destroy(vbh);
    
    bgfx::shutdown();
#ifdef __APPLE__
    if (metalView) SDL_Metal_DestroyView(metalView);
#endif
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

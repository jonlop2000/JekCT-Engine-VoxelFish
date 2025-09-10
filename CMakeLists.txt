cmake_minimum_required(VERSION 3.24)
project(voxel_fish LANGUAGES CXX)

# ---- Global settings ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Warnings
if (MSVC)
  add_compile_options(/W4 /Zc:__cplusplus /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- External: bgfx stack ----
# expects external/bgfx.cmake with bgfx/bx/bimg submodules
add_subdirectory(external/bgfx.cmake)

# ---- SDL2 ----
# Windows: vcpkg
# macOS:   Homebrew (brew install sdl2)
find_package(SDL2 CONFIG REQUIRED)

# ---- Tracy profiler ----
add_library(tracy_client STATIC external/tracy/public/TracyClient.cpp)
target_include_directories(tracy_client PUBLIC external/tracy/public)
target_compile_definitions(tracy_client PUBLIC TRACY_ENABLE)

# ---- App target ----
if(APPLE)
  # On mac we can switch to main.mm if we need Cocoa activation
  add_executable(fishbowl
    game/fishbowl/main.cpp
  )
else()
  add_executable(fishbowl
    game/fishbowl/main.cpp
  )
endif()

# ---- Platform specifics ----
if(WIN32)
    # Nothing special; vcpkg handles SDL2/bgfx/tracy
elseif(APPLE)
    # Build a .app bundle with Info.plist
    set_target_properties(fishbowl PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_GUI_IDENTIFIER com.jekct.voxelfish
      MACOSX_BUNDLE_BUNDLE_NAME "Voxel Fish"
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
    )

    # Link SDL2main so Cocoa entry point is set up correctly
    if(TARGET SDL2::SDL2main)
        target_link_libraries(fishbowl PRIVATE SDL2::SDL2main)
    endif()

    # Add rpath for Homebrew dylibs (so SDL2 is found at runtime)
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE BREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(BREW_PREFIX)
        set_target_properties(fishbowl PROPERTIES
            BUILD_RPATH "${BREW_PREFIX}/lib"
            INSTALL_RPATH "@executable_path/../Frameworks;@loader_path/../Frameworks;${BREW_PREFIX}/lib"
        )
    endif()

    # Explicitly link Cocoa framework (safe, avoids linker warnings)
    target_link_libraries(fishbowl PRIVATE "-framework Cocoa")
endif()

# ---- Link libraries ----
target_link_libraries(fishbowl
    PRIVATE
        SDL2::SDL2         # SDL2 core
        bgfx bx bimg       # bgfx stack
        tracy_client       # tracy profiler
)

# ---- Include dirs ----
target_include_directories(fishbowl PRIVATE
    ${CMAKE_SOURCE_DIR}/external/tracy/public
    ${CMAKE_SOURCE_DIR}/external/tracy/public/tracy
)

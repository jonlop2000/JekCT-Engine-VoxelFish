cmake_minimum_required(VERSION 3.24)
project(voxel_fish CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wno-unknown-pragmas)
endif()

# bgfx / bx / bimg via submodule
add_subdirectory(external/bgfx.cmake)

# SDL2 from vcpkg
find_package(SDL2 CONFIG QUIET)
if (NOT SDL2_FOUND)
  message(FATAL_ERROR "SDL2 not found. Install via vcpkg: vcpkg install sdl2:x64-windows")
endif()

# Tracy client
add_library(tracy_client STATIC external/tracy/public/TracyClient.cpp)
target_include_directories(tracy_client PUBLIC external/tracy/public)
target_compile_definitions(tracy_client PUBLIC TRACY_ENABLE)

# App target
add_executable(fishbowl game/fishbowl/main.cpp)

# Link SDL2 imported targets (vcpkg provides these)
if (TARGET SDL2::SDL2)
  target_link_libraries(fishbowl PRIVATE SDL2::SDL2)
  if (TARGET SDL2::SDL2main)
    target_link_libraries(fishbowl PRIVATE SDL2::SDL2main)
  endif()
else()
  message(FATAL_ERROR "SDL2 targets not available.")
endif()

target_link_libraries(fishbowl PRIVATE bgfx bx bimg tracy_client)

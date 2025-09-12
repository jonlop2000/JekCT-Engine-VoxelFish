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

# ---- bgfx stack (expects external/bgfx.cmake with bgfx/bx/bimg submodules) ----
add_subdirectory(external/bgfx.cmake)

# ---- Engine ----
add_subdirectory(engine)

# ---- SDL2 ----
# macOS: Homebrew (brew install sdl2) provides SDL2::SDL2 (and sometimes SDL2::SDL2main)
# Windows: vcpkg provides the same targets
find_package(SDL2 CONFIG REQUIRED)

# ---- Tracy profiler ----
add_library(tracy_client STATIC external/tracy/public/TracyClient.cpp)
target_include_directories(tracy_client PUBLIC external/tracy/public)
target_compile_definitions(tracy_client PUBLIC TRACY_ENABLE)

# ---- App target ----
add_executable(fishbowl
  game/fishbowl/main.cpp
)

# macOS bundle settings
if(APPLE)
  set_target_properties(fishbowl PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER com.jekct.voxelfish
    MACOSX_BUNDLE_BUNDLE_NAME "Voxel Fish"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
  )
endif()

# ---- Link libraries (deduped: DO NOT link bx explicitly) ----
# Prefer namespaced imported targets if available from bgfx.cmake, otherwise plain names.
if (TARGET bgfx::bgfx)
  target_link_libraries(fishbowl PRIVATE bgfx::bgfx)
elseif (TARGET bgfx)
  target_link_libraries(fishbowl PRIVATE bgfx)
else()
  message(FATAL_ERROR "bgfx target not found from external/bgfx.cmake")
endif()

if (TARGET bimg::bimg)
  target_link_libraries(fishbowl PRIVATE bimg::bimg)
elseif (TARGET bimg)
  target_link_libraries(fishbowl PRIVATE bimg)
else()
  message(FATAL_ERROR "bimg target not found from external/bgfx.cmake")
endif()

# SDL2 (and SDL2main on platforms that provide it)
target_link_libraries(fishbowl PRIVATE SDL2::SDL2)
if (TARGET SDL2::SDL2main)
  target_link_libraries(fishbowl PRIVATE SDL2::SDL2main)
endif()

# Tracy
target_link_libraries(fishbowl PRIVATE tracy_client)

# Engine
target_link_libraries(fishbowl PRIVATE engine_core)

# ---- macOS specifics ----
if(APPLE)
  # Add rpaths so bundled app finds Homebrew libs if needed
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
  else()
    set_target_properties(fishbowl PROPERTIES
      INSTALL_RPATH "@executable_path/../Frameworks;@loader_path/../Frameworks"
    )
  endif()

  # Frameworks commonly used by bgfx on macOS (safe to add; avoids missing symbol issues)
  target_link_libraries(fishbowl PRIVATE
    "-framework Cocoa"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework MetalKit"
    "-framework IOKit"
    "-framework CoreVideo"
  )
endif()

# ---- Include dirs (Tracy already PUBLIC on its target; keep local convenience) ----
target_include_directories(fishbowl PRIVATE
  ${CMAKE_SOURCE_DIR}/external/tracy/public
  ${CMAKE_SOURCE_DIR}/external/tracy/public/tracy
  ${CMAKE_SOURCE_DIR}/engine/runtime
)

# ---------- Shader compilation ----------
set(BGFX_DIR ${CMAKE_SOURCE_DIR}/external/bgfx.cmake/bgfx)
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_OUT ${CMAKE_BINARY_DIR}/generated/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})

if (NOT TARGET shaderc)
  message(FATAL_ERROR "bgfx shaderc target not found. Ensure external/bgfx.cmake is added.")
endif()

# Platform profiles
if (APPLE)
  set(SH_PLATFORM osx)
  set(VS_PROFILE metal)
  set(FS_PROFILE metal)
  elseif (WIN32)
  set(SH_PLATFORM windows)
  # Newer shaderc expects unified HLSL profile "s_5_0"
  set(HLSL_PROFILE s_5_0)
  set(VS_PROFILE ${HLSL_PROFILE})
  set(FS_PROFILE ${HLSL_PROFILE})

else()
  set(SH_PLATFORM linux)
  set(VS_PROFILE spirv)
  set(FS_PROFILE spirv)
endif()

# Vertex shader -> C header
add_custom_command(
  OUTPUT ${SHADER_OUT}/vs_simple.bin.h
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT}
  COMMAND $<TARGET_FILE:shaderc>
          --platform ${SH_PLATFORM} --type vertex --profile ${VS_PROFILE}
          -f ${SHADER_DIR}/vs_simple.sc
          -o ${SHADER_OUT}/vs_simple.bin.h
          --bin2c vs_simple
          --varyingdef ${SHADER_DIR}/varying.def.sc
          -i ${BGFX_DIR}/src
  DEPENDS shaderc ${SHADER_DIR}/vs_simple.sc ${SHADER_DIR}/varying.def.sc
  VERBATIM)

# Fragment shader -> C header
add_custom_command(
  OUTPUT ${SHADER_OUT}/fs_simple.bin.h
  COMMAND $<TARGET_FILE:shaderc>
          --platform ${SH_PLATFORM} --type fragment --profile ${FS_PROFILE}
          -f ${SHADER_DIR}/fs_simple.sc
          -o ${SHADER_OUT}/fs_simple.bin.h
          --bin2c fs_simple
          --varyingdef ${SHADER_DIR}/varying.def.sc
          -i ${BGFX_DIR}/src
  DEPENDS shaderc ${SHADER_DIR}/fs_simple.sc ${SHADER_DIR}/varying.def.sc
  VERBATIM)

add_custom_target(build_shaders
  DEPENDS ${SHADER_OUT}/vs_simple.bin.h ${SHADER_OUT}/fs_simple.bin.h)

# Make app depend on shaders; include generated headers
add_dependencies(fishbowl build_shaders)
target_include_directories(fishbowl PRIVATE ${SHADER_OUT})

# (Optional) Silence benign bx warning on Clang
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(fishbowl PRIVATE -Wno-gnu-zero-variadic-macro-arguments)
endif()

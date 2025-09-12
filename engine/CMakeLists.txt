# Header-only engine core
add_library(engine_core INTERFACE)

# Expose headers to dependents
target_include_directories(engine_core
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/runtime
)

# (Optional) enforce C++ standard on consumers
target_compile_features(engine_core INTERFACE cxx_std_20)

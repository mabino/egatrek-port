cmake_minimum_required(VERSION 3.28)
project(egatrek_port VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TREK_BUILD_APP "Build the SDL3 application" ON)
option(TREK_BUILD_TESTS "Build unit tests" ON)

# Flags for our own targets only (not third-party dependencies).
# The simulation must produce identical results on arm64 and wasm32 (Turbo Pascal 6-byte Reals get a software implementation):
# never fuse multiply-adds, never relax IEEE semantics.
add_library(trek_flags INTERFACE)
target_compile_options(trek_flags INTERFACE -ffp-contract=off -fno-fast-math -Wall -Wextra -Wpedantic)

if(EMSCRIPTEN)
  # Format readers report bad input with exceptions; use native wasm exception handling.
  add_compile_options(-fwasm-exceptions)
  add_link_options(-fwasm-exceptions)
endif()

if(TREK_BUILD_TESTS)
  enable_testing()
endif()

include(FetchContent)
add_subdirectory(engine)

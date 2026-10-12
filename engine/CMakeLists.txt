add_library(trek_core
  src/core/byte_reader.cpp
  src/core/data_path.cpp
  src/core/mz_image.cpp
  src/gfx/ega_surface.cpp
  src/gfx/stroke_font.cpp
)
target_include_directories(trek_core PUBLIC src)
target_link_libraries(trek_core PUBLIC trek_flags)

if(TREK_BUILD_APP)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz)
  FetchContent_MakeAvailable(SDL3)

  add_executable(egatrek MACOSX_BUNDLE src/app/main.cpp)
  target_link_libraries(egatrek PRIVATE trek_core SDL3::SDL3)
  set_target_properties(egatrek PROPERTIES
    OUTPUT_NAME "EGA Trek"
    MACOSX_BUNDLE_GUI_IDENTIFIER com.mabino.egatrek-port
    MACOSX_BUNDLE_BUNDLE_NAME "EGA Trek"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
  if(EMSCRIPTEN)
    # The page supplies TREK.EXE from the user's disk; nothing is preloaded.
    set_target_properties(egatrek PROPERTIES OUTPUT_NAME egatrek SUFFIX .html)
    target_link_options(egatrek PRIVATE -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1)
  endif()
endif()

if(TREK_BUILD_TESTS)
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz)
  set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(doctest)
  add_executable(trek_tests
    tests/main.cpp
    tests/byte_reader_test.cpp
    tests/data_path_test.cpp
    tests/mz_image_test.cpp
    tests/ega_surface_test.cpp
    tests/stroke_font_test.cpp
  )
  target_link_libraries(trek_tests PRIVATE trek_core doctest::doctest)
  add_test(NAME unit COMMAND trek_tests)
  if(TREK_BUILD_APP AND NOT EMSCRIPTEN)
    add_test(NAME app_smoke COMMAND egatrek --smoke)
    set_tests_properties(app_smoke PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
  endif()
endif()

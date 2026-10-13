add_library(trek_core
  src/core/byte_reader.cpp
  src/core/data_path.cpp
  src/core/mz_image.cpp
  src/gfx/ega_surface.cpp
  src/gfx/stroke_font.cpp
  src/dos/bios.cpp
  src/dos/dos.cpp
  src/dos/ega.cpp
  src/dos/interpreter.cpp
  src/dos/keymap.cpp
  src/dos/machine.cpp
  src/dos/rom_font.cpp
  src/dos/speaker.cpp
)
target_include_directories(trek_core PUBLIC src)
target_link_libraries(trek_core PUBLIC trek_flags)

# SDL is needed by the core too (logging), so it is always fetched.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz)
FetchContent_MakeAvailable(SDL3)
target_link_libraries(trek_core PUBLIC SDL3::SDL3)

# The recompiled game, generated from the user's TREK.EXE by scripts/recomp.zsh.
# Without it the app still builds and asks for the game.
set(TREK_GENERATED_DIR "${PROJECT_SOURCE_DIR}/build/generated" CACHE PATH "Output of trekre recomp")
file(GLOB TREK_GENERATED_SOURCES CONFIGURE_DEPENDS "${TREK_GENERATED_DIR}/*.cpp")
if(TREK_GENERATED_SOURCES)
  add_library(trek_game STATIC ${TREK_GENERATED_SOURCES})
  target_link_libraries(trek_game PUBLIC trek_core)
  # Huge functions of straight-line code: optimise lightly to keep build times sane.
  target_compile_options(trek_game PRIVATE -O1 -w)
  message(STATUS "EGA Trek: building recompiled game from ${TREK_GENERATED_DIR}")
  if(NOT EMSCRIPTEN)
    add_executable(trek_headless src/app/headless.cpp)
    target_link_libraries(trek_headless PRIVATE trek_game)
    add_executable(trek_lockstep src/app/lockstep.cpp)
    target_link_libraries(trek_lockstep PRIVATE trek_game)
  endif()
else()
  message(STATUS "EGA Trek: no recompiled game in ${TREK_GENERATED_DIR}; run scripts/recomp.zsh")
endif()

if(TREK_BUILD_APP)
  add_executable(egatrek MACOSX_BUNDLE src/app/main.cpp src/app/host_keys.cpp)
  target_link_libraries(egatrek PRIVATE trek_core SDL3::SDL3)
  if(TARGET trek_game)
    target_link_libraries(egatrek PRIVATE trek_game)
    target_compile_definitions(egatrek PRIVATE TREK_HAVE_GAME=1)
  endif()
  set_target_properties(egatrek PROPERTIES
    OUTPUT_NAME "EGA Trek"
    MACOSX_BUNDLE_GUI_IDENTIFIER com.mabino.egatrek-port
    MACOSX_BUNDLE_BUNDLE_NAME "EGA Trek"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
  if(EMSCRIPTEN)
    # The recompiled game (with its image) is compiled in; saves live in the browser.
    set_target_properties(egatrek PROPERTIES OUTPUT_NAME egatrek SUFFIX .html)
    target_link_options(egatrek PRIVATE -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 -lidbfs.js
      "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
    set_property(TARGET egatrek APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
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
    tests/machine_test.cpp
    tests/ega_test.cpp
  )
  target_link_libraries(trek_tests PRIVATE trek_core doctest::doctest)
  add_test(NAME unit COMMAND trek_tests)

  # Instruction corpus with Unicorn as the reference CPU, generated at build
  # time (needs uv; skipped where it is missing, e.g. in the wasm container).
  find_program(UV_EXECUTABLE uv)
  if(UV_EXECUTABLE AND NOT EMSCRIPTEN)
    set(CORPUS_DIR "${CMAKE_CURRENT_BINARY_DIR}/corpus")
    file(GLOB TREKRE_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tools/trekre/src/trekre/*.py")
    add_custom_command(
      OUTPUT "${CORPUS_DIR}/ops_corpus.inc" "${CORPUS_DIR}/ops_corpus_recomp.cpp"
      COMMAND "${UV_EXECUTABLE}" --directory "${PROJECT_SOURCE_DIR}/tools/trekre" run trekre corpus --out "${CORPUS_DIR}"
      DEPENDS ${TREKRE_SOURCES}
      COMMENT "Generating the instruction corpus with Unicorn"
      VERBATIM)
    add_executable(trek_corpus_tests tests/main.cpp tests/ops_corpus_test.cpp "${CORPUS_DIR}/ops_corpus_recomp.cpp")
    target_include_directories(trek_corpus_tests PRIVATE "${CORPUS_DIR}")
    target_compile_options(trek_corpus_tests PRIVATE -O1 -w)
    target_link_libraries(trek_corpus_tests PRIVATE trek_core doctest::doctest)
    add_test(NAME instruction_corpus COMMAND trek_corpus_tests)
  else()
    message(STATUS "EGA Trek: uv not found; instruction corpus test disabled")
  endif()
  # With the recompiled game: play a scripted session headless, and check it
  # block by block against the interpreter.
  if(TARGET trek_headless)
    set(TREK_TEST_KEYS " N{ENTER}N{ENTER}1{ENTER}KIRK{ENTER}{WAIT 200}L{ENTER}{WAIT 60}C{ENTER}{WAIT 60}R{ENTER}{WAIT 60}W{ENTER}6{ENTER}{WAIT 60}M{ENTER}5,5{ENTER}{WAIT 120}P{ENTER}{WAIT 60}SAVE{ENTER}{WAIT 60}{SF1}{WAIT 20}X{WAIT 20}")
    add_test(NAME game_headless COMMAND trek_headless --frames 2500 --keys "${TREK_TEST_KEYS}")
    add_test(NAME game_lockstep COMMAND trek_lockstep --seconds 240 --keys "${TREK_TEST_KEYS}")
    set_tests_properties(game_headless game_lockstep PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 600)
  endif()
  if(TREK_BUILD_APP AND NOT EMSCRIPTEN)
    add_test(NAME app_smoke COMMAND egatrek --smoke)
    set_tests_properties(app_smoke PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
  endif()
endif()

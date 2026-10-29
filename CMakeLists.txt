# gow3 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(gow3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Which Gears of War 3 executable to build: "tu6" (Title Update 6, needed for the DLC) or "base"
# (the disc version). Each has its own manifest, generated code and build folder.
set(GOW3_GAME_VERSION "base" CACHE STRING "Game version to build: base or tu6")
set_property(CACHE GOW3_GAME_VERSION PROPERTY STRINGS base tu6)
if(GOW3_GAME_VERSION STREQUAL "tu6")
    set(GOW3_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated/tu6")
    set(GOW3_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/gow3_manifest_tu6.toml")
elseif(GOW3_GAME_VERSION STREQUAL "base")
    set(GOW3_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated/default")
    set(GOW3_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/gow3_manifest.toml")
else()
    message(FATAL_ERROR "GOW3_GAME_VERSION must be base or tu6, not '${GOW3_GAME_VERSION}'")
endif()
include(cmake/gow3_rexglue.cmake)
add_subdirectory(setup)
enable_testing()
add_subdirectory(tests)

# Sources
set(GOW3_SOURCES
    src/main.cpp
    src/gow3_utf8_guard.cpp
    src/gow3_xam_input_guard.cpp
    src/gow3_xam_trace.cpp
    src/gow3_content_fix.cpp
    src/gow3_crash_log.cpp
    src/gow3_dlc.cpp
    src/gow3_title_update.cpp
    src/gow3_launch_check.cpp
    src/gow3_game_patches.cpp
    src/gow3_options_actions.cpp
    src/gow3_options_input.cpp
    src/gow3_options_overlay.cpp
    src/gow3_options_values.cpp
    src/gow3_mouse_aim.cpp
    src/gow3_coalesced.cpp
    src/gow3_button_glyphs.cpp
    src/gow3_unlocks.cpp
    src/gow3_loading_screen.cpp
    src/gow3_renderer.cpp
    src/gow3_main_menu_exit.cpp
    src/gow3_widescreen.cpp
    src/gow3_language.cpp
    src/gow3_keybind_text.cpp
)

if(WIN32)
    add_executable(gow3 WIN32 ${GOW3_SOURCES})
else()
    add_executable(gow3 ${GOW3_SOURCES})
endif()

rexglue_setup_target(gow3 GPU_PLUGINS xenos)
target_link_libraries(gow3 PRIVATE gow3_setup)
if(GOW3_GAME_VERSION STREQUAL "tu6")
    target_compile_definitions(gow3 PRIVATE GOW3_TU6=1)
endif()
# gow3.map: names the recompiled function (sub_<guest address>) at an offset a CRASHLOG line gives.
if(WIN32)
    target_link_options(gow3 PRIVATE "LINKER:/MAP:${CMAKE_CURRENT_BINARY_DIR}/gow3.map")
endif()

# ReXApp is compiled into the game from the SDK's share/rexglue/rex_app.cpp; use our copy instead
# (src/sdk/rex_app.cpp: the startup shader build behind a loading panel). SDK patches never reach
# that file, since it is built here against the SDK's installed copy.
get_target_property(GOW3_TARGET_SOURCES gow3 SOURCES)
list(TRANSFORM GOW3_TARGET_SOURCES REPLACE ".*/share/rexglue/rex_app\\.cpp$"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/rex_app.cpp")
set_target_properties(gow3 PROPERTIES SOURCES "${GOW3_TARGET_SOURCES}")

if(WIN32)
    target_link_libraries(gow3 PRIVATE dxgi ole32 shell32 user32 bcrypt)
endif()

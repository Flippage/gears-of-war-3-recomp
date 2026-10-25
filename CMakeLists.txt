# gow3 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(gow3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(GOW3_SOURCES
    src/main.cpp
    src/gow3_utf8_guard.cpp
    src/gow3_xam_input_guard.cpp
    src/gow3_xam_trace.cpp
    src/gow3_content_fix.cpp
    src/gow3_game_patches.cpp
    src/gow3_options_actions.cpp
    src/gow3_options_catalog.cpp
    src/gow3_options_input.cpp
    src/gow3_options_model.cpp
    src/gow3_options_overlay.cpp
    src/gow3_options_values.cpp
    src/gow3_options_values_text.cpp
    src/gow3_toml_edit.cpp
    src/gow3_mouse_aim.cpp
    src/gow3_coalesced.cpp
    src/gow3_button_glyphs.cpp
    src/gow3_unlocks.cpp
)

if(WIN32)
    add_executable(gow3 WIN32 ${GOW3_SOURCES})
else()
    add_executable(gow3 ${GOW3_SOURCES})
endif()

rexglue_setup_target(gow3 GPU_PLUGINS xenos)

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

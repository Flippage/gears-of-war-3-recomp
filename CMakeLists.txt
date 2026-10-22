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
)

if(WIN32)
    add_executable(gow3 WIN32 ${GOW3_SOURCES})
else()
    add_executable(gow3 ${GOW3_SOURCES})
endif()

rexglue_setup_target(gow3)

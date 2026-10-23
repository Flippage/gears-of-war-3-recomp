// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.
//
// Pure text helpers that translate between composite menu values
// ("window_mode", "window_size", "upscaler") and the underlying rex::cvar
// flag values.
#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace rex::glue::options {

// Strips outer quotes and unescapes `\"` and `\\` from FlagEntry::ToString().
std::string StripSurroundingQuotes(std::string_view raw);

// "windowed" / "borderless" / "exclusive" <-> (fullscreen, fullscreen_exclusive).
std::pair<std::string, std::string> SplitWindowMode(std::string_view mode);
std::string JoinWindowMode(std::string_view fullscreen, std::string_view exclusive);

// "1920x1080" / "0x0" <-> (window_width, window_height).
std::pair<std::string, std::string> SplitWindowSize(std::string_view size);
std::string JoinWindowSize(std::string_view width, std::string_view height);

// "off" / "auto" / "dlss" / "fsr3" / "fsr4" <-> (gow2_temporal, gow2_upscaler).
std::pair<std::string, std::string> SplitUpscaler(std::string_view upscaler);
std::string JoinUpscaler(std::string_view temporal, std::string_view upscaler);

}  // namespace rex::glue::options

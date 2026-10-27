// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow3-recomp contributors
// The arithmetic of the widescreen support (src/gow3_widescreen.cpp), header only so a host test can
// check it: which aspect a setting names, how wide the game's back buffer becomes, and how the field
// of view scales and widens.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace rex::glue::widescreen {

constexpr int kOriginalWidth = 1280;    // what the game builds at 16:9
constexpr int kTileWidth = 80;          // an EDRAM tile at 32 bits per pixel is 80 pixels wide
constexpr double kAutoMinAspect = 1.85; // above 16:9 (1.78) with room for 16:9.5 panels
constexpr int kMinWidth = 960;
// The widest back buffer for the EDRAM a renderer emulates. Gears 3's frame layout at 720 lines: scene
// depth from tile 0, scene colour after it (each width / 80 * 45 tiles), then the 1120x1120 shadow
// depth map (980 tiles) after the colour; the half-size post-process targets stay below that.
// The RTV path has the console's 2048 tiles: no wider than the game's own 1280 (at 1680 the
// post-process targets would wrap onto the scene depth). Native surfaces and ROV have 4096 virtual
// tiles: 2560 (32:9) reaches about tile 3860.
constexpr int kMaxWidthConsoleEdram = 1280;
constexpr int kMaxWidthVirtualEdram = 2560;

// "off", "", "0", "false" -> 0; "auto" -> display_aspect when it is wider than 16:9, else 0; "21:9" or "2.39" -> that
// aspect. Anything outside 1.2 .. 4 names no widescreen (0).
inline double ParseAspect(const std::string& text, double display_aspect) {
  if (text.empty() || text == "off" || text == "0" || text == "false") return 0.0;
  double a = 0.0;
  if (text == "auto") {
    // A 16:9 (or narrower) display is what the game already fills; only a wider one switches it on.
    a = display_aspect >= kAutoMinAspect ? display_aspect : 0.0;
  } else if (const size_t colon = text.find(':'); colon != std::string::npos) {
    const double w = std::atof(text.substr(0, colon).c_str());
    const double h = std::atof(text.substr(colon + 1).c_str());
    if (w > 0 && h > 0) a = w / h;
  } else {
    a = std::atof(text.c_str());
  }
  return (a >= 1.2 && a <= 4.0) ? a : 0.0;
}

// The back buffer width for an aspect at the given height: a multiple of 80 between 960 and max_width,
// or 0 when it is what the game builds anyway (1280 at 720 lines) or the aspect is 0.
inline int BackBufferWidth(double aspect, int height, int max_width) {
  if (aspect <= 0.0) return 0;
  int w = int(std::lround(aspect * height / double(kTileWidth))) * kTileWidth;
  if (w < kMinWidth) w = kMinWidth;
  if (w > max_width) w = max_width;
  return (w == kOriginalWidth && height == 720) ? 0 : w;
}

// The camera's half field of view (radians) scaled by the player's FOV multiplier and widened for a
// back buffer of the given width: the tangent grows by width / 1280 so the vertical view stays constant.
inline double ScaleAndWidenHalfFov(double half_fov, int width, double fov_multiplier) {
  if (fov_multiplier > 0.1 && fov_multiplier < 3.0 && std::fabs(fov_multiplier - 1.0) > 1e-4) {
    half_fov = half_fov * fov_multiplier;
    // Guard against singularity near pi/2 (1.570796 rad, 90 deg half FOV)
    if (half_fov > 1.48) half_fov = 1.48;
    if (half_fov < 0.10) half_fov = 0.10;
  }
  if (width > kOriginalWidth) {
    half_fov = std::atan(std::tan(half_fov) * (double(width) / double(kOriginalWidth)));
  }
  return half_fov;
}

}  // namespace rex::glue::widescreen

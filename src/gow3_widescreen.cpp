// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow3-recomp contributors
#include "gow3_widescreen.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <rex/cvar.h>
#include <rex/logging.h>

#include "gow3_widescreen_math.h"

REXCVAR_DEFINE_STRING(gow3_widescreen, "auto", "Display",
                      "Ultrawide support: auto (on when this PC's main display is wider than 16:9), off, or an "
                      "aspect such as 21:9 or 2.39. The game then renders wider instead of showing black bars; the "
                      "field of view is widened to keep the vertical view. Takes effect on the next start.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DECLARE(std::string, gow3_renderer);

REXCVAR_DEFINE_INT32(gow3_fov, 80, "Display",
                     "Player camera field of view (FOV) in degrees. 80 is default console FOV. "
                     "Higher values (up to 100) widen your peripheral vision. Takes effect immediately.");

namespace rex::glue {
namespace {

std::atomic<int> g_site_calls[8];
std::atomic<float> g_site_last[8];

double DisplayAspect() {
#if defined(_WIN32)
  const int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
  if (w > 0 && h > 0) return double(w) / double(h);
#endif
  return 0.0;
}

}  // namespace

double WidescreenAspect() {
  static const double aspect = [] {
    const std::string mode = REXCVAR_GET(gow3_widescreen);
    const double parsed = widescreen::ParseAspect(mode, DisplayAspect());
    return parsed;
  }();
  return aspect;
}

int WidescreenBackBufferHeight() {
  return 720;
}

int WidescreenBackBufferWidth() {
  static const int width = [] {
    // Wider than 16:9 needs the 4096-tile EDRAM of the native and ROV renderers.
    const bool console_edram = REXCVAR_GET(gow3_renderer) == "rtv";
    const int max_width =
        console_edram ? widescreen::kMaxWidthConsoleEdram : widescreen::kMaxWidthVirtualEdram;
    if (console_edram && WidescreenAspect() > 16.0 / 9.0 + 0.01) {
      REXLOG_INFO("WIDESCREEN off: wider than 16:9 needs the Native or ROV renderer (RTV has the "
                  "console's 10 MB of EDRAM)");
    }
    return widescreen::BackBufferWidth(WidescreenAspect(), WidescreenBackBufferHeight(), max_width);
  }();
  return width;
}

void PrepareWidescreen() {
  const int w = WidescreenBackBufferWidth();
  if (w <= 0) return;
  const int h = WidescreenBackBufferHeight();
  if (rex::cvar::GetFlagSource("video_mode_width") == rex::cvar::Source::kDefault)
    (void)rex::cvar::SetFlagByName("video_mode_width", std::to_string(w));
  if (rex::cvar::GetFlagSource("video_mode_height") == rex::cvar::Source::kDefault)
    (void)rex::cvar::SetFlagByName("video_mode_height", std::to_string(h));
  REXLOG_INFO("WIDESCREEN gow3_widescreen '{}' -> aspect {:.4f}, back buffer {}x{}", REXCVAR_GET(gow3_widescreen),
              WidescreenAspect(), w, h);
}

double WidescreenHalfFov(double half_fov, int site) {
  const int w = WidescreenBackBufferWidth();
  const int fov = REXCVAR_GET(gow3_fov);
  const double mult = (fov >= 40 && fov <= 140) ? (double(fov) / 80.0) : 1.0;
  if (w <= 0 && fov == 80) return half_fov;
  const double scaled_and_widened = widescreen::ScaleAndWidenHalfFov(half_fov, w, mult);
  if (site >= 0 && site < 8) {
    g_site_calls[site].fetch_add(1, std::memory_order_relaxed);
    g_site_last[site].store(float(half_fov * 360.0 / 3.14159265358979), std::memory_order_relaxed);
  }
  static std::atomic<int> logged[8];
  if (site >= 0 && site < 8 && logged[site].fetch_add(1) < 3) {
    REXLOG_INFO("WIDESCREEN site {} half fov {:.4f} rad ({:.1f} deg) [setting {} deg, mult {:.3f}] -> {:.4f} rad ({:.1f} deg)",
                site, half_fov, half_fov * 360.0 / 3.14159265358979, fov, mult, scaled_and_widened,
                scaled_and_widened * 360.0 / 3.14159265358979);
  }
  return scaled_and_widened;
}

}  // namespace rex::glue

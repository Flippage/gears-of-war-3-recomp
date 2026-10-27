// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow3-recomp contributors
// Widescreen support: back buffer width and Hor+ field of view scaling for Gears of War 3.
#pragma once

#include <cstdint>

namespace rex::glue {

// The aspect ratio wanted (width / height), or 0 when the game keeps its own 16:9.
double WidescreenAspect();
// The back buffer width the game will build (a multiple of 80), or 0 when off.
int WidescreenBackBufferWidth();
// The back buffer height the game uses (720).
int WidescreenBackBufferHeight();
// Runs from OnPreSetup: gives the runtime's video mode the same aspect so the presenter shows the
// frame unstretched.
void PrepareWidescreen();
// The camera's half field of view (radians) scaled and widened for the player's FOV multiplier and
// back buffer width.
double WidescreenHalfFov(double half_fov, int site);

}  // namespace rex::glue

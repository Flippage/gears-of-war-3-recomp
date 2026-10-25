// gow3 - ReXGlue Recompiled Project
//
// The renderer choice (gow3_renderer, PC Options -> Graphics): native (default: host render
// targets with the native surfaces and native resolves of SDK patches 0069-0082, ported from the
// Gears of War 2 port), rtv (host render targets, emulated EDRAM copies) or rov (the EDRAM emulated
// in the pixel shader: the most exact, the slowest).
#pragma once

namespace rex::glue {

// Before the GPU plugin loads (OnPreSetup). Sets the plugin's own settings as defaults, so a value
// given in gow3.toml or on the command line still wins.
void ApplyRendererChoice();

}  // namespace rex::glue

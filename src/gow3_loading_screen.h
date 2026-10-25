// gow3 - ReXGlue Recompiled Project
//
// The startup "PREPARING SHADERS" screen (drawn by ShaderLoadingDialog in src/sdk/rex_app.cpp):
// a spinning cog over a purple progress bar, drawn straight onto the black window.
#pragma once

#include <cstdint>

struct ImFont;
struct ImGuiIO;

namespace rex::glue {

// Draws the whole screen for one frame. done / total: pipelines built / to build (total == 0
// while the shaders are still read). Either font may be nullptr (the default font is used).
void DrawShaderLoadingScreen(ImGuiIO& io, uint32_t done, uint32_t total, float elapsed_s,
                             ImFont* title_font, ImFont* body_font);

}  // namespace rex::glue

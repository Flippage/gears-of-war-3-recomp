// gow3 - ReXGlue Recompiled Project
//
// Switches for the patch sites tools/patch_game_patches.py writes into the generated code.
// Each one is a cvar (gow3.toml or --name=value), so a patch can be A/B'd without a rebuild.
// Defaults follow the player's Xenia selection for this game where there was one.

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(gow3_black_shading_fix, true, "GameFixes",
                    "Black Shading Fix: GUseTilingMode=FALSE, against the black blocks MSAA "
                    "resolves leave in character shadows. Read when the renderer starts.");
REXCVAR_DEFINE_BOOL(gow3_unlock_fps, true, "GameFixes",
                    "Unlock FPS: skips the engine frame-rate cap and its frame mode.");
REXCVAR_DEFINE_BOOL(gow3_occlusion_query_fix, false, "GameFixes",
                    "Occlusion Query Fix: occlusion tests against full-resolution depth, less "
                    "geometry popping.");
REXCVAR_DEFINE_BOOL(gow3_disable_lens_flares, false, "GameFixes", "Disable Lens Flares.");
REXCVAR_DEFINE_BOOL(gow3_disable_ao, false, "GameFixes", "Disable Ambient Occlusion.");
REXCVAR_DEFINE_BOOL(gow3_disable_motion_blur, true, "GameFixes",
                    "Disable Motion Blur: no ghosting and artifacts when the camera pans quickly.");
REXCVAR_DEFINE_BOOL(gow3_aniso16, true, "GameFixes", "16x Anisotropic Filtering.");
REXCVAR_DEFINE_BOOL(gow3_resolution_scaling_fix, false, "GameFixes",
                    "Resolution Scaling Fix / Disable All Post-Processing: changes the overall "
                    "look, but no edge-detection overcorrection or colour smearing.");
REXCVAR_DEFINE_BOOL(gow3_disable_dof, false, "GameFixes", "Disable Depth of Field.");
REXCVAR_DEFINE_BOOL(gow3_disable_bloom, false, "GameFixes", "Disable Bloom.");
REXCVAR_DEFINE_INT32(gow3_texture_pool_mb, 196, "GameFixes",
                     "Texture streaming pool in MB (the game's own is 145; 0 keeps it). A bigger "
                     "pool evicts and reloads fewer textures while moving through a level; the "
                     "Gears of War 2 port uses 196.");

REXCVAR_DEFINE_BOOL(gow3_shadow_perf_b, false, "GameFixes",
                    "Performance Patch Option B (Smart Shadow Cuts): at most 20 per-object shadow "
                    "depth draws a frame, no pre-allocated shadow caching (which breaks on NVIDIA), "
                    "OneFrameRenderLag off.");
REXCVAR_DEFINE_BOOL(gow3_disable_dynamic_shadows, false, "GameFixes",
                    "No dynamic shadows at all (Performance Patch Option A without its broken byte).");

#include "gow3_widescreen.h"

extern "C" double Gow3WidescreenHalfFov(double half_fov, int site) {
  return rex::glue::WidescreenHalfFov(half_fov, site);
}

extern "C" int Gow3GamePatch(int id) {
  switch (id) {
    case 1: return REXCVAR_GET(gow3_black_shading_fix);
    case 2: return REXCVAR_GET(gow3_unlock_fps);
    case 3: return REXCVAR_GET(gow3_occlusion_query_fix);
    case 4: return REXCVAR_GET(gow3_disable_lens_flares);
    case 5: return REXCVAR_GET(gow3_disable_ao);
    case 6: return REXCVAR_GET(gow3_disable_motion_blur);
    case 7: return REXCVAR_GET(gow3_aniso16);
    case 8: return REXCVAR_GET(gow3_resolution_scaling_fix);
    case 9: return REXCVAR_GET(gow3_disable_dof);
    case 10: return REXCVAR_GET(gow3_disable_bloom);
    case 11: return REXCVAR_GET(gow3_texture_pool_mb);
    case 12: return REXCVAR_GET(gow3_shadow_perf_b);
    case 13: return REXCVAR_GET(gow3_disable_dynamic_shadows);
    case 14: return rex::glue::WidescreenBackBufferWidth();
    default: return 0;
  }
}

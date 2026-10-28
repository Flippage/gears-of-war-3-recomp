// gow3 - ReXGlue Recompiled Project
//
// The renderer choice, see gow3_renderer.h.

#include "gow3_renderer.h"

#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(gow3_renderer, "native", "GPU",
                      "Renderer: native (host render targets with native surfaces and resolves, "
                      "the fastest), rtv (host render targets), rov (EDRAM in the pixel shader, the "
                      "most exact and the slowest); restart")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rex::glue {

void ApplyRendererChoice() {
  const std::string choice = REXCVAR_GET(gow3_renderer);
  const bool native = choice != "rtv" && choice != "rov";
  // The plugin registers these when it loads; set before that, they become its defaults (SDK
  // patch 0071), and gow3.toml or the command line still override them.
  rex::cvar::SetFlagByName("render_target_path_d3d12", choice == "rov" ? "rov" : "rtv");
  rex::cvar::SetFlagByName("gpu_native_surfaces", native ? "true" : "false");
  rex::cvar::SetFlagByName("gpu_native_resolves", native ? "true" : "false");
  // The HUD reaches the upscaler no more (gow2_hud_separate); the old UI mask, which let parts of
  // the scene fall back to the raw frame, stays off on the frames without the HUD-free copy too.
  rex::cvar::SetFlagByName("gow2_ui_mask", "false");
  REXLOG_INFO("Renderer: {}", native ? "native" : choice);
}

}  // namespace rex::glue

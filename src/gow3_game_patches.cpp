// gow3 - ReXGlue Recompiled Project
//
// Switches for the patch sites tools/patch_game_patches.py writes into the generated code.
// Each one is a cvar (gow3.toml or --name=value), so a patch can be A/B'd without a rebuild.

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(gow3_black_shading_fix, true, "GameFixes",
                    "Black Shading Fix: GUseTilingMode=FALSE, which removes the black blocks that "
                    "MSAA resolves leave in character shadows (in-engine cutscenes). Read when "
                    "the renderer starts.");

extern "C" int Gow3GamePatch(int id) {
  switch (id) {
    case 1: return REXCVAR_GET(gow3_black_shading_fix);
    default: return 0;
  }
}

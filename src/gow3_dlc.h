// gow3 - ReXGlue Recompiled Project
//
// Installs the player's own DLC packages (Xbox 360 STFS files) into the content folder. See
// gow3_dlc.cpp.

#pragma once

#include <filesystem>

namespace rex::glue {

// Installs every DLC package found in the DLC folders that is not installed yet. Call once the
// game's executable is loaded (OnPostLoadXexImage), before guest code runs.
void InstallPlayerDlc(const std::filesystem::path& game_folder);

}  // namespace rex::glue

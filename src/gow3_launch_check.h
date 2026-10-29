// gow3 - ReXGlue Recompiled Project
//
// Before the runtime starts: hand over to launcher.exe (setup, offer, repair) when it is needed.

#pragma once

#include <filesystem>

namespace rex::glue {

// Call from OnFinalizePaths, once gow3.toml is loaded, before PrepareTitleUpdate. Returns normally to
// start the game. When launcher.exe sits next to gow3.exe and setup is needed it starts
// `launcher.exe --setup|--offer|--repair -- <original args>` and exits the process.
void RunLaunchCheck(const std::filesystem::path& game_data_root);

}  // namespace rex::glue

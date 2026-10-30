// gow3 - ReXGlue Recompiled Project
//
// Before the runtime starts: hand over to launcher.exe (setup, offer, repair) when it is needed.

#pragma once

#include <filesystem>

namespace rex::glue {

// Called from ReXApp::OnInitialize (src/sdk/rex_app.cpp) once gow3.toml is loaded and the game folder
// resolved, before the window and graphics exist (a hand-over from exclusive fullscreen could hang on
// a black screen). Returns normally to start the game. When launcher.exe sits next to gow3.exe and setup is needed it starts
// `launcher.exe --setup|--offer|--repair -- <original args>` and exits the process.
void RunLaunchCheck(const std::filesystem::path& game_data_root);

}  // namespace rex::glue

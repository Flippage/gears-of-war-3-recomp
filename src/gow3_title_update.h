// gow3 - ReXGlue Recompiled Project
//
// Title Update 6 for the TU6 executable (GOW3_TU6): finds the player's extracted TU6 files and
// lays them over the game folder. See gow3_title_update.cpp.

#pragma once

#include <filesystem>
#include <optional>

namespace rex::glue {

// The folder holding the player's TU6 default.xexp (gow3_tu6_folder, else <exe>\TU6, else
// <game>\TU6), or nothing.
std::optional<std::filesystem::path> FindTitleUpdateFolder(const std::filesystem::path& game_folder);

// TU6 executable only (does nothing in the base one). Call once gow3.toml is loaded and before the
// runtime mounts its devices (OnFinalizePaths): sets update_root to the TU6 folder (the game's
// UPDATE: drive) and lays default.xexp over the game folder. Without TU6's default.xexp it explains
// what is missing and exits: TU6's code cannot run on the disc executable's image.
void PrepareTitleUpdate(const std::filesystem::path& game_folder, std::filesystem::path& update_root);

}  // namespace rex::glue

// gow3 - ReXGlue Recompiled Project
//
// The TU6 executable is recompiled from the disc's default.xex with Title Update 6's default.xexp
// applied, so at run time the loader has to apply the same patch to the same disc image, and the
// update's files have to be on the UPDATE: drive: the game reads a file from UPDATE:\<path> instead
// of the disc when UPDATE:\<path>_us exists (GearGame\Patches\ScriptPatch_*.bin_us; without the
// script patches the classes do not match TU6's code). They come from the player's own TU6,
// extracted to a folder: gow3_tu6_folder, else a TU6 folder next to gow3.exe or inside the game
// folder. That folder becomes UPDATE:, and SDK patch 0092 (GOW3_VFS_OVERLAY) shows its default.xexp
// in the game folder, where the loader looks for it; nothing is copied into the game folder.

#include "gow3_title_update.h"

#include <windows.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "gow3_addresses.h"

REXCVAR_DEFINE_STRING(gow3_tu6_folder, "", "Content",
                      "Folder with your own extracted Title Update 6 (default.xexp, GearGame, TU__ "
                      "files). Empty: a TU6 folder next to gow3.exe or inside the game folder");

namespace rex::glue {

// The folders searched for the player's TU6, in order.
static std::vector<std::filesystem::path> TitleUpdateCandidates(const std::filesystem::path& game_folder) {
  std::vector<std::filesystem::path> candidates;
  const std::string configured = REXCVAR_GET(gow3_tu6_folder);
  if (!configured.empty()) {
    candidates.push_back(std::filesystem::u8path(configured));
  } else {
    candidates.push_back(rex::filesystem::GetExecutablePath().parent_path() / "TU6");
    candidates.push_back(game_folder / "TU6");
  }
  return candidates;
}

std::optional<std::filesystem::path> FindTitleUpdateFolder(const std::filesystem::path& game_folder) {
  std::error_code ec;
  for (const std::filesystem::path& folder : TitleUpdateCandidates(game_folder)) {
    if (std::filesystem::is_regular_file(folder / "default.xexp", ec)) return folder;
  }
  return std::nullopt;
}

void PrepareTitleUpdate(const std::filesystem::path& game_folder, std::filesystem::path& update_root) {
  if (!addr::kTu6) return;
  std::error_code ec;
  if (const auto folder = FindTitleUpdateFolder(game_folder)) {
    const std::string overlay = rex::path_to_utf8(std::filesystem::absolute(game_folder, ec)) + "|" +
                                rex::path_to_utf8(std::filesystem::absolute(*folder, ec));
    _putenv_s("GOW3_VFS_OVERLAY", overlay.c_str());
    update_root = std::filesystem::absolute(*folder, ec);
    REXLOG_INFO("TU6: title update from {}", rex::path_to_utf8(*folder));
    return;
  }
  std::string where;
  for (const std::filesystem::path& folder : TitleUpdateCandidates(game_folder)) where += "\n" + rex::path_to_utf8(folder);
  REXLOG_ERROR("TU6: default.xexp not found in:{}", where);
  rex::FlushLogging();
  const std::string text =
      "This build of Gears of War 3 needs Title Update 6, extracted from your own copy (see the "
      "README), and its default.xexp was not found in:" + where +
      "\n\nSet gow3_tu6_folder in gow3.toml to the folder, or use the base-game build.";
  MessageBoxA(nullptr, text.c_str(), "Gears of War 3: Title Update 6 missing", MB_OK | MB_ICONERROR);
  std::exit(1);
}

}  // namespace rex::glue

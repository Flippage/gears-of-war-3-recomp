// gow3 - ReXGlue Recompiled Project
//
// Player-supplied DLC: Xbox 360 marketplace packages (STFS files starting with "LIVE", "PIRS" or
// "CON ") put in a DLC folder are installed with the SDK's ContentManager::InstallContent, which
// extracts them to <content root>/0000000000000000/4D5308AB/00000002/<file name>/ and writes the
// header file the game's content enumeration reads. A package counts as installed once its header
// exists (InstallContent writes it last), so an interrupted install is redone on the next start.
// Searched: gow3_dlc_folder if set, else a DLC folder next to gow3.exe and one inside the game
// folder, each recursively (the packages keep their console layout, 4D5308AB\00000002\<name>).

#include "gow3_dlc.h"

#include <array>
#include <chrono>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>

REXCVAR_DEFINE_STRING(gow3_dlc_folder, "", "Content",
                      "Folder with your own Gears of War 3 DLC packages (searched recursively). "
                      "Empty: a DLC folder next to gow3.exe and one inside the game folder");

namespace rex::glue {
namespace {

bool IsStfsPackage(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::array<char, 4> magic{};
  if (!in.read(magic.data(), magic.size())) return false;
  const std::string m(magic.data(), magic.size());
  return m == "LIVE" || m == "PIRS" || m == "CON ";
}

std::vector<std::filesystem::path> DlcFolders(const std::filesystem::path& game_folder) {
  const std::string configured = REXCVAR_GET(gow3_dlc_folder);
  if (!configured.empty()) return {std::filesystem::u8path(configured)};
  std::vector<std::filesystem::path> folders = {
      rex::filesystem::GetExecutablePath().parent_path() / "DLC"};
  if (!game_folder.empty()) folders.push_back(game_folder / "DLC");
  return folders;
}

}  // namespace

void InstallPlayerDlc(const std::filesystem::path& game_folder) {
  auto* ks = rex::system::kernel_state();
  auto* content = ks ? ks->content_manager() : nullptr;
  if (!content) return;
  const uint32_t title_id = ks->title_id();

  for (const std::filesystem::path& folder : DlcFolders(game_folder)) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) continue;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec) || !IsStfsPackage(it->path())) continue;
      const std::string name = rex::path_to_utf8(it->path().filename());
      rex::system::xam::XCONTENT_AGGREGATE_DATA installed{};
      if (content->ReadContentHeaderFile(name, 0, title_id,
                                         rex::system::XContentType::kMarketplaceContent,
                                         installed) == X_ERROR_SUCCESS) {
        REXLOG_INFO("DLC already installed: {} ({})", rex::path_to_utf8(installed.display_name()),
                    name);
        continue;
      }
      REXLOG_INFO("DLC installing {} ...", rex::path_to_utf8(it->path()));
      const auto start = std::chrono::steady_clock::now();
      const X_RESULT result = content->InstallContent(it->path());
      const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
      if (result == X_ERROR_SUCCESS) {
        REXLOG_INFO("DLC installed {} in {:.1f} s", name, seconds.count());
      } else {
        REXLOG_ERROR("DLC install of {} failed: 0x{:08X}", name, uint32_t(result));
      }
    }
  }
}

}  // namespace rex::glue

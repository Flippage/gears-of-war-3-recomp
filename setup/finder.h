#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "setup/stfs.h"
#include "setup/xex_info.h"

namespace gow3::setup {

inline constexpr uint32_t kGow3TitleId = 0x4D5308AB, kTuType = 0x000B0000, kDlcType = 0x00000002;

// Written into the target folder before an extraction starts and removed when it succeeds.
inline constexpr const wchar_t* kExtractingMarker = L".gow3-extracting";

// A game folder holds default.xex and GearGame\, and no kExtractingMarker (an interrupted extraction).
bool IsGameFolder(const std::filesystem::path& dir);
// The folder itself or one of its direct subfolders; empty when neither is a game folder.
std::filesystem::path FindGameFolderIn(const std::filesystem::path& dir);
// The game folder gow3.exe uses, in this order: `given` (--game_data_root) when it is a game folder,
// the first line of <exe_dir>\gow3_game_path.txt (or a subfolder of it), then exe_dir and up to four
// parent folders, each with its direct subfolders. `given` when nothing is found.
std::filesystem::path ResolveGameFolder(const std::filesystem::path& given, const std::filesystem::path& exe_dir);
// The value of --game_data_root=<v> (or --game_data_root <v>) in a command line's arguments, split
// as gow3.exe splits them (CommandLineToArgvW). Empty when it isn't there.
std::filesystem::path GameDataRootArg(const std::wstring& args);

// Where the player's extracted Title Update 6 is looked for, in order: `configured`
// (gow3_tu6_folder) when set, else <exe_dir>\TU6, then <game>\TU6.
std::vector<std::filesystem::path> TitleUpdateCandidates(const std::filesystem::path& game,
                                                         const std::filesystem::path& exe_dir,
                                                         const std::filesystem::path& configured);
// The first candidate holding default.xexp.
std::optional<std::filesystem::path> FindTitleUpdateFolderIn(const std::filesystem::path& game,
                                                             const std::filesystem::path& exe_dir,
                                                             const std::filesystem::path& configured);
// start, then up to `parents` parent folders.
std::vector<std::filesystem::path> SearchRoots(const std::filesystem::path& start, int parents);
// Where gow3.exe and the launcher look for packages: SearchRoots(exe_dir, 2), then the game folder
// and its parent (left out when `game` is empty).
std::vector<std::filesystem::path> PackageRoots(const std::filesystem::path& exe_dir,
                                                const std::filesystem::path& game);

struct FoundIso {
  std::filesystem::path file;
  uint64_t size;
  XexInfo xex;
};
struct FoundPackage {
  std::filesystem::path file;
  uint64_t size;
  std::string id;  // PackageId
  StfsHeader header;
  bool is_tu;
  bool matches_disc;  // TU: media id equals the disc's; DLC: always true
};
struct FindResult {
  std::optional<std::filesystem::path> game;
  std::vector<FoundIso> isos;
};

// Each root and its direct subfolders: the first game folder found. Only when there is none, every Gears 3 ISO
// (a found game folder returns at once with `isos` empty, so no image is opened).
FindResult FindGame(const std::vector<std::filesystem::path>& roots);
// Direct files of each root and of its direct subfolders: Gears 3 TU and DLC packages.
std::vector<FoundPackage> FindPackages(const std::vector<std::filesystem::path>& roots,
                                       uint32_t disc_media_id);

}  // namespace gow3::setup

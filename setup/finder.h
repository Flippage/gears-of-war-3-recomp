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

// A game folder holds default.xex and GearGame\.
bool IsGameFolder(const std::filesystem::path& dir);
// start, then up to `parents` parent folders.
std::vector<std::filesystem::path> SearchRoots(const std::filesystem::path& start, int parents);

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

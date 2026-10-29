#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace gow3::setup {

struct StfsHeader {
  std::string magic;  // "CON ", "LIVE", "PIRS"
  uint32_t content_type = 0, title_id = 0, media_id = 0;
  std::string display_name;  // UTF-8, from the UTF-16BE field at 0x411
};

// Reads the first 0x1000 bytes only. nullopt when unreadable or not an STFS package.
std::optional<StfsHeader> ReadStfsHeader(const std::filesystem::path& file);

// Called at least once per extracted file; return false to cancel.
using Progress = std::function<bool(uint64_t done, uint64_t total, const std::string& current)>;
enum class ExtractResult { kOk, kCancelled, kFailed };

// Extracts every file of the package under out_dir. A file in progress is written to
// <name>.part and renamed when complete; it is deleted on cancel or failure.
ExtractResult ExtractStfs(const std::filesystem::path& package, const std::filesystem::path& out_dir,
                          const Progress& progress, std::string* error);

}  // namespace gow3::setup

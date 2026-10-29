#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace gow3::setup {

enum class Build { kBase, kTu6 };

struct LauncherState {
  Build build = Build::kBase;
  std::set<std::string> declined, installed;
  std::string skip_release;
  bool check_updates = true;
  int64_t last_check = 0;
  bool existed = false;  // false when launcher.ini was missing (v0.2.2 or older install)
};

// launcher.ini is `key = value` lines; unknown keys are ignored on load and dropped on save.
// declined / installed are package ids separated by '|' (a file name can hold a comma, not a '|').
LauncherState LoadState(const std::filesystem::path& ini);
// Writes <ini>.tmp, then replaces the target.
bool SaveState(const std::filesystem::path& ini, const LauncherState& s);

// First line, UTF-8, trailing \r and spaces trimmed. Empty path when missing.
std::filesystem::path ReadGamePathFile(const std::filesystem::path& txt);
// UTF-8, no BOM.
bool WriteGamePathFile(const std::filesystem::path& txt, const std::filesystem::path& game);

// "v0.3.0" / "0.3.0" / "0.3" (missing patch = 0).
std::optional<std::array<int, 3>> ParseVersion(std::string_view v);
// False if either side fails to parse.
bool IsNewer(std::string_view candidate, std::string_view current);

// "<file name>:<size>"
std::string PackageId(const std::filesystem::path& file, uint64_t size);

}  // namespace gow3::setup

#pragma once
// The file-level rules of a release update (the launcher's ApplyUpdate does the I/O).
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gow3::setup {

// "<hash>  <zip name>" (package.ps1): the first field, lower-cased, when it is 64 hex digits.
std::optional<std::string> ParseSha256File(std::string_view text);

// The first line of a `tar -tf` listing that SafeName rejects (it would land outside the
// extraction folder); nullopt when every entry is safe.
std::optional<std::string> UnsafeEntry(std::string_view listing);

// Regular files under src (the zip's gow3-recomp-<v>-win64 folder), relative, that the update
// copies over the release. Left out: links, names SafeName rejects, anything under update.tmp, and
// at the top level gow3.toml, gow3_game_path.txt, launcher.ini (kept), gow3.exe (SwapBuild writes
// it), launcher.old.exe and version.txt (copied by ApplyUpdate after the swap). Names compare
// case-insensitively.
std::vector<std::filesystem::path> FilesToCopy(const std::filesystem::path& src);

}  // namespace gow3::setup

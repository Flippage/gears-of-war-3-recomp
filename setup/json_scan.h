#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gow3::setup {

// The fields the launcher reads from GitHub's releases/latest answer.
struct ReleaseInfo {
  std::string tag;
  bool prerelease = false;
  std::string body;
  std::vector<std::string> asset_names;
};

// Tokenizes the whole document (strings with escapes, \uXXXX and surrogate pairs as UTF-8, numbers,
// true/false/null, nested objects and arrays) and reads only the top-level tag_name, prerelease and
// body, and assets[].name. Malformed JSON, a top level that isn't an object, a read field of the
// wrong type, or nesting deeper than 64 -> nullopt. Missing fields keep their defaults.
std::optional<ReleaseInfo> ParseRelease(std::string_view json);

}  // namespace gow3::setup

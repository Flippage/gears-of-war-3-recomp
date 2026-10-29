#pragma once
#include <filesystem>
#include <string>

#include "setup/stfs.h"

namespace gow3::setup {

// Records the message (when wanted) and returns kFailed.
inline ExtractResult Fail(std::string* error, const std::string& msg) {
  if (error) *error = msg;
  return ExtractResult::kFailed;
}

inline std::string Utf8(const std::filesystem::path& p) {
  auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

// Entry paths may not climb out of the output folder or name another drive.
inline bool SafeName(const std::string& rel) {
  if (rel.empty() || rel[0] == '/' || rel[0] == '\\') return false;
  return rel.find("..") == std::string::npos && rel.find(':') == std::string::npos;
}

}  // namespace gow3::setup

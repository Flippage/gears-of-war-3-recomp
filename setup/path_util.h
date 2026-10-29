#pragma once
#include <cstdint>
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

inline std::filesystem::path FromUtf8(const std::string& s) {
  return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

// One code point as UTF-8.
inline void AppendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// The folder of the running exe (GetModuleFileNameW, any length); path_util.cpp.
std::filesystem::path ExeFolder();

// Entry paths may not climb out of the output folder or name another drive.
inline bool SafeName(const std::string& rel) {
  if (rel.empty() || rel[0] == '/' || rel[0] == '\\') return false;
  return rel.find("..") == std::string::npos && rel.find(':') == std::string::npos;
}

}  // namespace gow3::setup

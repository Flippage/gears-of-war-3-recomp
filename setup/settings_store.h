#pragma once
// gow3.toml as the launcher's settings tabs see it: the file's values as text, a Model over them,
// and a save that rewrites only the lines the player changed.
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

#include "gow3_options_model.h"

namespace gow3::setup {

struct SettingsFile {
  bool readable = true;
  std::string error;                          // parse error text
  std::map<std::string, std::string> values;  // raw keys -> text (strings unquoted)
};

// A missing file is readable and empty.
SettingsFile LoadSettings(const std::filesystem::path& toml);
// Reader: composite joins, else the file's value, else the row default. No writer.
rex::glue::options::Model MakeSettingsModel(const SettingsFile& f);
// The TOML literal for a key's text: typed by the key's catalog row (or the raw composite part).
std::string TomlLiteralFor(std::string_view key, std::string_view value);
// Writes the touched rows only (composites expanded to their raw keys). False, and nothing
// written, when the file is unreadable. No touched rows writes nothing.
bool SaveSettings(const std::filesystem::path& toml, const rex::glue::options::Model& m);

}  // namespace gow3::setup

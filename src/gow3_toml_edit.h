// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.
//
// In-place updater for gow3.toml. Unlike rex::cvar::SaveConfig, which overwrites
// the file with bare "name = value" lines, UpdateTomlFile keeps the user's
// comments, section headers and blank lines intact.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rex::glue {

using TomlEntry = std::pair<std::string, std::string>;  // cvar name -> raw TOML literal

// Returns the updated TOML document text for `existing_content`.
// Each entry in `entries` replaces an active `key = ...` line in place,
// uncommenting a `# key = ...` template line if no active assignment exists,
// or appending at the end of the file as a last resort.
std::string UpdateTomlText(std::string_view existing_content,
                           const std::vector<TomlEntry>& entries);

// Reads `path` (if it exists), applies `entries` via UpdateTomlText, and
// writes the result atomically via a sibling `.tmp` file.
bool UpdateTomlFile(const std::filesystem::path& path, const std::vector<TomlEntry>& entries);

// Formats a cvar string representation (from GetFlagByName()->ToString()) as a
// valid TOML literal (`true`/`false`, integer/float, or quoted string).
std::string FormatTomlLiteral(std::string_view cvar_to_string);

}  // namespace rex::glue

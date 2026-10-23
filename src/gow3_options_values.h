// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.
//
// Bridges the PC Options Model to the live rex::cvar registry and gow3.toml.
// Translates composite rows ("window_mode", "window_size", "upscaler") to and
// from their underlying cvars and populates runtime-discovered choices
// (monitors, GPUs, save folders).
#pragma once

#include "gow3_options_model.h"

#include <filesystem>
#include <string>
#include <string_view>

#include <rex/rex_app.h>

namespace rex::glue::options {

// Reads or writes a single catalog row (including composite rows) against
// the live rex::cvar registry.
std::string ReadRowValue(std::string_view cvar);
void WriteRowValue(std::string_view cvar, std::string_view value);

// Builds a Model wired to ReadRowValue / WriteRowValue and populates dynamic
// choices for monitors, GPUs, and save folders.
Model CreateLiveModel(const rex::PathConfig& paths);

// Re-queries monitors, GPUs, and save folders and updates the model's dynamic
// choice lists.
void RefreshDynamicRowChoices(Model& model, const rex::PathConfig& paths);

// Writes every row touched in `model` during this session to `config_path`
// while preserving comments and formatting in gow3.toml.
bool SaveConfigToDisk(const std::filesystem::path& config_path, const Model& model);

}  // namespace rex::glue::options

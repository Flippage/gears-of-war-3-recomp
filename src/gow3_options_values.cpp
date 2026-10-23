// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.

#include "gow3_options_values.h"

#include "gow3_options_actions.h"
#include "gow3_options_values_text.h"
#include "gow3_toml_edit.h"

#include <unordered_set>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>

namespace rex::glue::options {
namespace {

std::string ReadFlag(std::string_view name) {
  if (rex::cvar::GetFlagInfo(name)) {
    return StripSurroundingQuotes(rex::cvar::GetFlagByName(name));
  }
  return {};
}

bool IsCompositeRow(std::string_view cvar) {
  return cvar == "window_mode" || cvar == "window_size" || cvar == "upscaler";
}

}  // namespace

std::string ReadRowValue(std::string_view cvar) {
  if (cvar == "window_mode") {
    return JoinWindowMode(ReadFlag("fullscreen"), ReadFlag("fullscreen_exclusive"));
  }
  if (cvar == "window_size") {
    return JoinWindowSize(ReadFlag("window_width"), ReadFlag("window_height"));
  }
  if (cvar == "upscaler") {
    return JoinUpscaler(ReadFlag("gow2_temporal"), ReadFlag("gow2_upscaler"));
  }
  return ReadFlag(cvar);
}

void WriteRowValue(std::string_view cvar, std::string_view value) {
  if (cvar == "window_mode") {
    auto [fs, ex] = SplitWindowMode(value);
    rex::cvar::SetFlagByName("fullscreen", fs);
    rex::cvar::SetFlagByName("fullscreen_exclusive", ex);
    return;
  }
  if (cvar == "window_size") {
    auto [w, h] = SplitWindowSize(value);
    rex::cvar::SetFlagByName("window_width", w);
    rex::cvar::SetFlagByName("window_height", h);
    return;
  }
  if (cvar == "upscaler") {
    auto [temporal, upscaler] = SplitUpscaler(value);
    rex::cvar::SetFlagByName("gow2_temporal", temporal);
    rex::cvar::SetFlagByName("gow2_upscaler", upscaler);
    return;
  }
  rex::cvar::SetFlagByName(cvar, value);
}

void RefreshDynamicRowChoices(Model& model, const rex::PathConfig& paths) {
  // Monitors.
  {
    std::vector<DynamicChoice> choices;
    for (const auto& m : options_actions::ListMonitors()) {
      choices.push_back({std::to_string(m.index), m.label});
    }
    if (!choices.empty()) {
      model.SetDynamicChoices("monitor", std::move(choices));
    }
  }

  // Direct3D 12 GPUs (d3d12_adapter: -1 = Automatic, 0..N = DXGI hardware adapter index).
  {
    std::vector<DynamicChoice> choices;
    choices.push_back({"-1", "Automatic"});
    for (const auto& g : options_actions::ListGpus()) {
      choices.push_back({std::to_string(g.index), g.label});
    }
    model.SetDynamicChoices("d3d12_adapter", std::move(choices));
  }

  // Save folders (user_data_root: "" = default AppData folder, or custom path).
  {
    std::filesystem::path current_save_dir = paths.user_data_root;
    const std::string configured = model.Get("user_data_root");
    if (!configured.empty()) {
      current_save_dir = rex::to_path(configured);
    }
    std::vector<DynamicChoice> choices;
    for (const auto& f : options_actions::ListSaveFolders(current_save_dir)) {
      choices.push_back({f.value, f.label});
    }
    if (!choices.empty()) {
      model.SetDynamicChoices("user_data_root", std::move(choices));
    }
  }
}

Model CreateLiveModel(const rex::PathConfig& paths) {
  Model model(&ReadRowValue, &WriteRowValue);

  for (const auto& page : Catalog()) {
    for (const auto& row : page.rows) {
      if (row.control == Control::kAction) {
        continue;
      }
      if (row.cvar == std::string_view("upscaler")) {
        if (!rex::cvar::GetFlagInfo("gow2_temporal") ||
            !rex::cvar::GetFlagInfo("gow2_upscaler")) {
          model.AddUnavailable(row.cvar);
        }
        continue;
      }
      if (IsCompositeRow(row.cvar)) {
        continue;
      }
      if (!rex::cvar::GetFlagInfo(row.cvar)) {
        model.AddUnavailable(row.cvar);
      }
    }
  }

  RefreshDynamicRowChoices(model, paths);
  return model;
}

bool SaveConfigToDisk(const std::filesystem::path& config_path, const Model& model) {
  if (config_path.empty()) {
    return false;
  }

  std::vector<std::string> cvar_names;
  auto push_unique = [&](std::string name) {
    for (const auto& existing : cvar_names) {
      if (existing == name) {
        return;
      }
    }
    cvar_names.push_back(std::move(name));
  };

  for (const auto& row_key : model.TouchedRows()) {
    if (row_key == "window_mode") {
      push_unique("fullscreen");
      push_unique("fullscreen_exclusive");
    } else if (row_key == "window_size") {
      push_unique("window_width");
      push_unique("window_height");
    } else if (row_key == "upscaler") {
      push_unique("gow2_temporal");
      push_unique("gow2_upscaler");
    } else {
      push_unique(row_key);
    }
  }

  if (cvar_names.empty()) {
    return true;
  }

  std::vector<TomlEntry> entries;
  entries.reserve(cvar_names.size());
  for (const auto& name : cvar_names) {
    if (rex::cvar::GetFlagInfo(name)) {
      entries.emplace_back(name, FormatTomlLiteral(rex::cvar::GetFlagByName(name)));
    }
  }

  if (entries.empty()) {
    return true;
  }
  return UpdateTomlFile(config_path, entries);
}

}  // namespace rex::glue::options

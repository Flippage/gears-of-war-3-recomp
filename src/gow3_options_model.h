// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.
//
// Testable state model for the PC Options menu. Holds the value of every
// catalog row, tracks which rows have changed since the game booted (so the
// UI can show "Restart required" honestly), and drives step/toggle/reset
// actions without touching ImGui or the live cvar registry directly.
#pragma once

#include "gow3_options_catalog.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rex::glue::options {

struct DynamicChoice {
  std::string value;
  std::string label;
};

class Model {
 public:
  using ReadFn = std::function<std::string(std::string_view cvar)>;
  using WriteFn = std::function<void(std::string_view cvar, std::string_view value)>;

  explicit Model(ReadFn reader = {}, WriteFn writer = {});

  // Reloads current values from the reader without forgetting the boot baseline.
  void RefreshFromReader();

  std::string Get(std::string_view cvar) const;
  bool GetBool(std::string_view cvar) const;
  int GetInt(std::string_view cvar) const;
  double GetFloat(std::string_view cvar) const;

  void Set(std::string_view cvar, std::string_view value);
  void Toggle(std::string_view cvar);
  void StepChoice(std::string_view cvar, int delta);
  void StepSlider(std::string_view cvar, int delta);
  void SetSliderNormalized(std::string_view cvar, double t);
  double SliderNormalized(std::string_view cvar) const;

  // Dynamic choices override a row's static Catalog choices (used for
  // monitors, GPUs, and save folders discovered at runtime).
  void SetDynamicChoices(std::string_view cvar, std::vector<DynamicChoice> choices);
  std::vector<DynamicChoice> ChoicesFor(const RowSpec& row, Lang lang) const;

  bool IsModifiedFromBoot(std::string_view cvar) const;
  bool HasPendingRestart() const;
  bool HasPendingNextLevel() const;

  void ResetPageToDefaults(Page page);

  // Marks a row as unavailable in the current build (e.g. a cvar not compiled
  // into the current runtime). Unavailable rows are hidden by the overlay.
  void AddUnavailable(std::string_view cvar);
  bool IsAvailable(std::string_view cvar) const;

  // Rows changed by the player in this session. SaveConfigToDisk writes only
  // these keys so opening the menu never clobbers untouched lines in gow3.toml.
  const std::unordered_set<std::string>& TouchedRows() const { return touched_; }

 private:
  void ApplySingle(std::string_view cvar, std::string_view value);

  ReadFn reader_;
  WriteFn writer_;
  std::unordered_map<std::string, std::string> boot_values_;
  std::unordered_map<std::string, std::string> current_values_;
  std::unordered_map<std::string, std::vector<DynamicChoice>> dynamic_choices_;
  std::unordered_set<std::string> unavailable_;
  std::unordered_set<std::string> touched_;
};

}  // namespace rex::glue::options

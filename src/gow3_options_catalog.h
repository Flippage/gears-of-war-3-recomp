// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.
//
// Player-facing options catalog for the Gears of War 3 PC Options menu.
// Pure data and lookup functions (no ImGui, no runtime dependency).
#pragma once

#include <string_view>
#include <vector>

namespace rex::glue::options {

enum class Page {
  kDisplay,
  kGraphics,
  kControls,
  kPadButtons,
  kKeyBindings,
  kNetwork,
  kSaves,
};

// When a change takes effect. Shown on every row and used to drive the
// "Restart required" banner at the bottom of the menu.
enum class Apply {
  kNow,
  kNextLevel,
  kRestart,
};

enum class Control {
  kToggle,       // bool ("true" / "false")
  kChoice,       // discrete list of string/int values
  kSliderInt,    // integer range
  kSliderFloat,  // double range
  kText,         // free text (IP, player name, path)
  kAction,       // opens a sub-page or triggers an action
};

enum class Lang {
  kEn,
};

struct Text {
  const char* en = "";

  const char* For(Lang /*lang*/ = Lang::kEn) const { return en ? en : ""; }
};

struct Choice {
  const char* value;  // Stored in the cvar / TOML as this text.
  Text label;
};

struct RowSpec {
  const char* cvar;  // Cvar name in rex::cvar, or composite id ("window_mode", "window_size", "upscaler").
  Page page;
  Control control;
  Apply apply;
  Text label;
  Text help;
  const char* default_value;
  std::vector<Choice> choices;  // For kChoice.
  double min_value = 0.0;       // For kSliderInt / kSliderFloat.
  double max_value = 0.0;
  double step = 1.0;
  const char* unit = "";
};

struct PageSpec {
  Page id;
  Text title;
  Text subtitle;
  std::vector<RowSpec> rows;
};

const std::vector<PageSpec>& Catalog();
const RowSpec* FindRow(std::string_view cvar);
const Text& UiText(std::string_view key);
Lang CurrentLang();

}  // namespace rex::glue::options

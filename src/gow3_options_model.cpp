// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.

#include "gow3_options_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace rex::glue::options {
namespace {

std::string FormatSliderValue(const RowSpec& row, double v) {
  if (row.control == Control::kSliderInt) {
    long iv = std::lround(v);
    return std::to_string(iv);
  }
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.2f", v);
  std::string s(buf);
  while (s.size() > 1 && s.find('.') != std::string::npos &&
         (s.back() == '0' || s.back() == '.')) {
    char dropped = s.back();
    s.pop_back();
    if (dropped == '.') {
      break;
    }
  }
  return s;
}

bool ValuesEquivalent(const RowSpec* row, std::string_view a, std::string_view b) {
  if (a == b) {
    return true;
  }
  if (!row) {
    return false;
  }
  if (row->control == Control::kSliderFloat || row->control == Control::kSliderInt) {
    char* end_a = nullptr;
    char* end_b = nullptr;
    std::string sa(a);
    std::string sb(b);
    double va = std::strtod(sa.c_str(), &end_a);
    double vb = std::strtod(sb.c_str(), &end_b);
    if (end_a != sa.c_str() && end_b != sb.c_str()) {
      return std::fabs(va - vb) < 1e-6;
    }
  }
  return false;
}

}  // namespace

Model::Model(ReadFn reader, WriteFn writer)
    : reader_(std::move(reader)), writer_(std::move(writer)) {
  for (const auto& page : Catalog()) {
    for (const auto& row : page.rows) {
      std::string initial = row.default_value ? row.default_value : "";
      if (reader_) {
        std::string from_runtime = reader_(row.cvar);
        if (!from_runtime.empty() || row.control == Control::kText ||
            row.cvar == std::string_view("user_data_root")) {
          initial = std::move(from_runtime);
        }
      }
      boot_values_[row.cvar] = initial;
      current_values_[row.cvar] = std::move(initial);
    }
  }
}

void Model::RefreshFromReader() {
  if (!reader_) {
    return;
  }
  for (const auto& page : Catalog()) {
    for (const auto& row : page.rows) {
      std::string v = reader_(row.cvar);
      if (!v.empty() || row.control == Control::kText ||
          row.cvar == std::string_view("user_data_root")) {
        current_values_[row.cvar] = std::move(v);
      }
    }
  }
}

std::string Model::Get(std::string_view cvar) const {
  auto it = current_values_.find(std::string(cvar));
  if (it != current_values_.end()) {
    return it->second;
  }
  if (const auto* row = FindRow(cvar)) {
    return row->default_value ? row->default_value : "";
  }
  return {};
}

bool Model::GetBool(std::string_view cvar) const {
  auto v = Get(cvar);
  return v == "true" || v == "1";
}

int Model::GetInt(std::string_view cvar) const {
  auto v = Get(cvar);
  return std::atoi(v.c_str());
}

double Model::GetFloat(std::string_view cvar) const {
  auto v = Get(cvar);
  return std::atof(v.c_str());
}

void Model::ApplySingle(std::string_view cvar, std::string_view value) {
  std::string key(cvar);
  std::string val(value);
  current_values_[key] = val;
  touched_.insert(key);
  if (writer_) {
    writer_(key, val);
  }
}

void Model::Set(std::string_view cvar, std::string_view value) {
  ApplySingle(cvar, value);
}

void Model::Toggle(std::string_view cvar) {
  const auto* row = FindRow(cvar);
  if (!row) {
    return;
  }
  if (row->control == Control::kToggle) {
    Set(cvar, GetBool(cvar) ? "false" : "true");
  } else if (row->control == Control::kChoice) {
    StepChoice(cvar, +1);
  }
}

void Model::SetDynamicChoices(std::string_view cvar, std::vector<DynamicChoice> choices) {
  dynamic_choices_[std::string(cvar)] = std::move(choices);
}

std::vector<DynamicChoice> Model::ChoicesFor(const RowSpec& row, Lang lang) const {
  auto it = dynamic_choices_.find(row.cvar);
  if (it != dynamic_choices_.end() && !it->second.empty()) {
    return it->second;
  }
  std::vector<DynamicChoice> out;
  out.reserve(row.choices.size());
  for (const auto& c : row.choices) {
    out.push_back({c.value, c.label.For(lang)});
  }
  return out;
}

void Model::StepChoice(std::string_view cvar, int delta) {
  const auto* row = FindRow(cvar);
  if (!row || row->control != Control::kChoice) {
    return;
  }
  auto choices = ChoicesFor(*row, Lang::kEn);
  if (choices.empty()) {
    return;
  }
  std::string cur = Get(cvar);
  int idx = 0;
  for (size_t i = 0; i < choices.size(); ++i) {
    if (choices[i].value == cur) {
      idx = static_cast<int>(i);
      break;
    }
  }
  int n = static_cast<int>(choices.size());
  int next = ((idx + delta) % n + n) % n;
  Set(cvar, choices[static_cast<size_t>(next)].value);
}

void Model::StepSlider(std::string_view cvar, int delta) {
  const auto* row = FindRow(cvar);
  if (!row || (row->control != Control::kSliderInt && row->control != Control::kSliderFloat)) {
    return;
  }
  double v = GetFloat(cvar) + static_cast<double>(delta) * row->step;
  v = std::clamp(v, row->min_value, row->max_value);
  if (row->step > 0.0) {
    double steps = std::round((v - row->min_value) / row->step);
    v = std::clamp(row->min_value + steps * row->step, row->min_value, row->max_value);
  }
  Set(cvar, FormatSliderValue(*row, v));
}

void Model::SetSliderNormalized(std::string_view cvar, double t) {
  const auto* row = FindRow(cvar);
  if (!row || (row->control != Control::kSliderInt && row->control != Control::kSliderFloat)) {
    return;
  }
  t = std::clamp(t, 0.0, 1.0);
  double v = row->min_value + t * (row->max_value - row->min_value);
  if (row->step > 0.0) {
    double steps = std::round((v - row->min_value) / row->step);
    v = std::clamp(row->min_value + steps * row->step, row->min_value, row->max_value);
  }
  Set(cvar, FormatSliderValue(*row, v));
}

double Model::SliderNormalized(std::string_view cvar) const {
  const auto* row = FindRow(cvar);
  if (!row || row->max_value <= row->min_value) {
    return 0.0;
  }
  double v = std::clamp(GetFloat(cvar), row->min_value, row->max_value);
  return (v - row->min_value) / (row->max_value - row->min_value);
}

bool Model::IsModifiedFromBoot(std::string_view cvar) const {
  auto boot_it = boot_values_.find(std::string(cvar));
  auto cur_it = current_values_.find(std::string(cvar));
  if (boot_it == boot_values_.end() || cur_it == current_values_.end()) {
    return false;
  }
  return !ValuesEquivalent(FindRow(cvar), boot_it->second, cur_it->second);
}

bool Model::HasPendingRestart() const {
  for (const auto& page : Catalog()) {
    for (const auto& row : page.rows) {
      if (row.apply == Apply::kRestart && IsModifiedFromBoot(row.cvar)) {
        return true;
      }
    }
  }
  return false;
}

bool Model::HasPendingNextLevel() const {
  for (const auto& page : Catalog()) {
    for (const auto& row : page.rows) {
      if (row.apply == Apply::kNextLevel && IsModifiedFromBoot(row.cvar)) {
        return true;
      }
    }
  }
  return false;
}

void Model::ResetPageToDefaults(Page page) {
  for (const auto& p : Catalog()) {
    if (p.id != page) {
      continue;
    }
    for (const auto& row : p.rows) {
      if (row.control == Control::kAction) {
        continue;
      }
      if (!IsAvailable(row.cvar)) {
        continue;
      }
      ApplySingle(row.cvar, row.default_value ? row.default_value : "");
    }
  }
}

void Model::AddUnavailable(std::string_view cvar) {
  unavailable_.emplace(cvar);
}

bool Model::IsAvailable(std::string_view cvar) const {
  return unavailable_.find(std::string(cvar)) == unavailable_.end();
}

}  // namespace rex::glue::options

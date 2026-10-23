// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.

#include "gow3_options_values_text.h"

namespace rex::glue::options {

std::string StripSurroundingQuotes(std::string_view s) {
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    s = s.substr(1, s.size() - 2);
  }
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == '\\' || s[i + 1] == '"')) {
      out.push_back(s[i + 1]);
      ++i;
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

std::pair<std::string, std::string> SplitWindowMode(std::string_view mode) {
  if (mode == "windowed") {
    return {"false", "false"};
  }
  if (mode == "exclusive") {
    return {"true", "true"};
  }
  return {"true", "false"};
}

std::string JoinWindowMode(std::string_view fullscreen, std::string_view exclusive) {
  const bool fs = (fullscreen == "true" || fullscreen == "1");
  const bool ex = (exclusive == "true" || exclusive == "1");
  if (!fs) {
    return "windowed";
  }
  return ex ? "exclusive" : "borderless";
}

std::pair<std::string, std::string> SplitWindowSize(std::string_view size) {
  auto x = size.find('x');
  if (x == std::string_view::npos) {
    return {"0", "0"};
  }
  return {std::string(size.substr(0, x)), std::string(size.substr(x + 1))};
}

std::string JoinWindowSize(std::string_view w, std::string_view h) {
  if ((w == "0" || w.empty()) && (h == "0" || h.empty())) {
    return "0x0";
  }
  return std::string(w) + "x" + std::string(h);
}

std::pair<std::string, std::string> SplitUpscaler(std::string_view upscaler) {
  if (upscaler.empty() || upscaler == "off") {
    return {"false", "off"};
  }
  return {"true", std::string(upscaler)};
}

std::string JoinUpscaler(std::string_view temporal, std::string_view upscaler) {
  const bool on = (temporal == "true" || temporal == "1");
  if (!on || upscaler.empty() || upscaler == "off") {
    return "off";
  }
  return std::string(upscaler);
}

}  // namespace rex::glue::options

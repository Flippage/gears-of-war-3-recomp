// Copy of the patched runtime's rex/input/pad_remap.h (Gears of War 2 port SDK patch 0029,
// BSD-3-Clause, Santiago Quiroz Upegui and the gow2-recomp contributors): the game builds against the
// stock SDK headers, which do not have it. Keep in sync with sdk/patches.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace rex::input::pad_remap {

struct Button {
  std::string_view name;
  uint16_t bit;
};

inline constexpr std::array<Button, 14> kButtons = {{
    {"a", 0x1000},
    {"b", 0x2000},
    {"x", 0x4000},
    {"y", 0x8000},
    {"lb", 0x0100},
    {"rb", 0x0200},
    {"back", 0x0020},
    {"start", 0x0010},
    {"ls", 0x0040},
    {"rs", 0x0080},
    {"dpad_up", 0x0001},
    {"dpad_down", 0x0002},
    {"dpad_left", 0x0004},
    {"dpad_right", 0x0008},
}};

constexpr uint16_t SingleBitForName(std::string_view name) {
  for (const auto& btn : kButtons) {
    if (btn.name == name) {
      return btn.bit;
    }
  }
  return 0;
}

constexpr uint16_t BitForName(std::string_view name) {
  uint16_t mask = 0;
  size_t start = 0;
  while (start < name.size()) {
    while (start < name.size() && (name[start] == ' ' || name[start] == ',' || name[start] == '\t')) {
      ++start;
    }
    if (start >= name.size()) {
      break;
    }
    size_t end = start;
    while (end < name.size() && name[end] != ',') {
      ++end;
    }
    size_t token_end = end;
    while (token_end > start && (name[token_end - 1] == ' ' || name[token_end - 1] == '\t')) {
      --token_end;
    }
    std::string_view token = name.substr(start, token_end - start);
    mask |= SingleBitForName(token);
    start = end;
  }
  return mask;
}

constexpr uint16_t Remap(uint16_t physical, const std::array<uint16_t, 14>& sources) {
  uint16_t result = 0;
  for (size_t i = 0; i < sources.size(); ++i) {
    if (sources[i] != 0 && (physical & sources[i]) != 0) {
      result |= kButtons[i].bit;
    }
  }
  // Pass through bits outside the 14 remapped buttons unchanged.
  uint16_t all_remapped_bits = 0;
  for (const auto& btn : kButtons) {
    all_remapped_bits |= btn.bit;
  }
  result |= (physical & ~all_remapped_bits);
  return result;
}

constexpr std::array<uint16_t, 14> IdentitySources() {
  std::array<uint16_t, 14> src{};
  for (size_t i = 0; i < 14; ++i) {
    src[i] = kButtons[i].bit;
  }
  return src;
}

}  // namespace rex::input::pad_remap

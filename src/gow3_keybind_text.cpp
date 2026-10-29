// gow3 - ReXGlue Recompiled Project
//
// Key-binding text helpers, see gow3_keybind_text.h. Moved verbatim from gow3_options_overlay.cpp.

#include "gow3_keybind_text.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <rex/ui/keybinds.h>
#endif

namespace rex::glue::options {

std::vector<std::string> SplitKeys(std::string_view val) {
  std::vector<std::string> keys;
  size_t start = 0;
  while (start < val.size()) {
    size_t comma = val.find(',', start);
    if (comma == std::string_view::npos) {
      comma = val.size();
    }
    std::string_view part = val.substr(start, comma - start);
    while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) {
      part.remove_prefix(1);
    }
    while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) {
      part.remove_suffix(1);
    }
    if (!part.empty()) {
      keys.emplace_back(part);
    }
    start = comma + 1;
  }
  return keys;
}

std::string JoinKeys(const std::vector<std::string>& keys) {
  std::string result;
  for (size_t i = 0; i < keys.size(); ++i) {
    if (i > 0) result += ",";
    result += keys[i];
  }
  return result;
}

#ifdef _WIN32
// The first key or mouse button down, as a keybind name ("" if none).
// Shift, Ctrl and Alt bind as keys under their generic names (the left / right variants are
// skipped: the keyboard/mouse driver records the generic ones); Escape (cancel), F10 and the
// Windows keys are skipped.
std::string FirstBindableKeyDown() {
  for (int vk = 0x01; vk <= 0xFE; ++vk) {
    if (vk == VK_ESCAPE || vk == VK_F10 || (vk >= VK_LSHIFT && vk <= VK_RMENU) || vk == VK_LWIN ||
        vk == VK_RWIN) {
      continue;
    }
    if (!(GetAsyncKeyState(vk) & 0x8000)) {
      continue;
    }
    std::string name = rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(vk));
    if (!name.empty()) {
      return name;
    }
  }
  return {};
}
#endif

}  // namespace rex::glue::options

// gow3 - ReXGlue Recompiled Project
//
// Key-binding text shared by the in-game options overlay and the launcher: a binding is a
// comma-separated list of key names ("w,up").
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rex::glue::options {

// "a, b ,c" -> {"a","b","c"}: trims blanks, drops empty parts.
std::vector<std::string> SplitKeys(std::string_view val);
std::string JoinKeys(const std::vector<std::string>& keys);

#ifdef _WIN32
// The first key or mouse button down, as a keybind name ("" if none). Needs the rex runtime.
std::string FirstBindableKeyDown();
#endif

}  // namespace rex::glue::options

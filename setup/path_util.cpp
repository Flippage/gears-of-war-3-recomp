#include "setup/path_util.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace gow3::setup {

std::filesystem::path ExeFolder() {
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0) return {};
    if (n < buf.size()) return std::filesystem::path(buf.substr(0, n)).parent_path();
    buf.resize(buf.size() * 2);
  }
}

}  // namespace gow3::setup

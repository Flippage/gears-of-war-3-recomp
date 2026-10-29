#include "test_util.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

static fs::path ExeDir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4];
  DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
  return fs::path(std::wstring(buf, n)).parent_path();
#else
  return fs::read_symlink("/proc/self/exe").parent_path();
#endif
}

fs::path TempDir(const std::string& name) {
  fs::path dir = ExeDir() / "test-tmp" / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

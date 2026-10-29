#define _CRT_SECURE_NO_WARNINGS
#include "test_util.h"

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

static fs::path ExeDir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4];
  DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
  if (n == 0 || n >= std::size(buf)) {
    // TempDir deletes folders; never guess a path.
    std::fprintf(stderr, "test-util: cannot determine test exe path\n");
    std::exit(2);
  }
  return fs::path(std::wstring(buf, n)).parent_path();
#else
  return fs::read_symlink("/proc/self/exe").parent_path();
#endif
}

fs::path TempDir(const std::string& name) {
  fs::path dir = ExeDir() / "test-tmp" / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  if (ec) std::fprintf(stderr, "test-util: remove_all(%s) failed: %s\n", dir.string().c_str(), ec.message().c_str());
  ec.clear();
  fs::create_directories(dir, ec);
  if (ec) std::fprintf(stderr, "test-util: create_directories(%s) failed: %s\n", dir.string().c_str(), ec.message().c_str());
  return dir;
}

std::optional<fs::path> EnvPath(const char* name) {
#ifdef _WIN32
  std::wstring wname(name, name + std::strlen(name));
  const wchar_t* v = _wgetenv(wname.c_str());
  if (!v || !*v) return std::nullopt;
  return fs::path(v);
#else
  const char* v = std::getenv(name);
  if (!v || !*v) return std::nullopt;
  return fs::path(v);
#endif
}

static void PutBe32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[off + i] = static_cast<uint8_t>(v >> (24 - 8 * i));
}

std::vector<uint8_t> MakeXex(uint32_t media_id, uint32_t title_id) {
  std::vector<uint8_t> b(0x200, 0);
  b[0] = 'X'; b[1] = 'E'; b[2] = 'X'; b[3] = '2';
  PutBe32(b, 0x14, 1);
  PutBe32(b, 0x18, 0x00040006);
  PutBe32(b, 0x1C, 0x100);
  PutBe32(b, 0x100, media_id);
  PutBe32(b, 0x10C, title_id);
  return b;
}

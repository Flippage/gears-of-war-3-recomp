#pragma once
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gow3_test {
struct Test {
  const char* name;
  void (*fn)();
};
std::vector<Test>& Registry();
extern int g_failed_checks;
struct Registrar {
  Registrar(const char* n, void (*f)()) { Registry().push_back({n, f}); }
};
}  // namespace gow3_test

#define TEST(name)                                                  \
  static void test_##name();                                        \
  static gow3_test::Registrar registrar_##name(#name, &test_##name); \
  static void test_##name()

// Records file:line and keeps going.
#define CHECK(expr)                                                        \
  do {                                                                     \
    if (!(expr)) {                                                         \
      ++gow3_test::g_failed_checks;                                        \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
    }                                                                      \
  } while (0)

// <exe dir>\test-tmp\<name>, emptied first.
std::filesystem::path TempDir(const std::string& name);

// Environment variable as a path; nullopt when unset or empty.
std::optional<std::filesystem::path> EnvPath(const char* name);

// Minimal XEX2 header (0x200 bytes) with one execution-info entry.
std::vector<uint8_t> MakeXex(uint32_t media_id, uint32_t title_id);

// Writes (truncating) a file; returns false on failure.
bool WriteFile(const std::filesystem::path& path, std::string_view data);
bool WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& data);
// Whole file as text / bytes; empty when unreadable.
std::string FileText(const std::filesystem::path& path);
std::vector<uint8_t> FileBytes(const std::filesystem::path& path);

// 0x1000-byte STFS header with the fields the launcher reads (magic, content type, media id,
// title id, UTF-16BE display name).
std::vector<uint8_t> MakeStfsHeader(std::string_view magic, uint32_t content_type, uint32_t title_id,
                                    uint32_t media_id, std::u16string_view display_name);
// True when both folders hold the same relative file paths with the same bytes.
bool SameTree(const std::filesystem::path& a, const std::filesystem::path& b);
// True when any file under dir has a name ending in suffix.
bool AnyFileEndsWith(const std::filesystem::path& dir, const std::string& suffix);

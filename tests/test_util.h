#pragma once
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
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

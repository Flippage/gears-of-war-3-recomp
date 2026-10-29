#include <cstdio>
#include <vector>

#include "test_util.h"

namespace gow3_test {
std::vector<Test>& Registry() {
  static std::vector<Test> r;
  return r;
}
int g_failed_checks = 0;
}  // namespace gow3_test

int main() {
  int failed = 0;
  for (const auto& t : gow3_test::Registry()) {
    int before = gow3_test::g_failed_checks;
    t.fn();
    if (gow3_test::g_failed_checks != before) {
      ++failed;
      std::printf("FAIL %s\n", t.name);
    }
  }
  int total = static_cast<int>(gow3_test::Registry().size());
  std::printf("%d passed, %d failed\n", total - failed, failed);
  return failed != 0;
}

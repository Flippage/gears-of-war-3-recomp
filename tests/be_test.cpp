#include <cstdint>

#include "setup/be.h"
#include "test_util.h"

TEST(be_reads) {
  const uint8_t b[] = {0x4D, 0x53, 0x08, 0xAB, 0x01};
  CHECK(gow3::setup::Be32(b, 0) == 0x4D5308ABu);
  CHECK(gow3::setup::Le16(b, 0) == 0x534Du);
  CHECK(gow3::setup::Be32(b, 2) == 0);  // past the end
}

#include "setup/xex_info.h"

#include "test_util.h"

TEST(xex_execution_info) {
  auto info = gow3::setup::ReadXexInfo(MakeXex(0x58122AD6, 0x4D5308AB));
  CHECK(info && info->title_id == 0x4D5308AB && info->media_id == 0x58122AD6);
  CHECK(info && gow3::setup::Hex8(info->media_id) == "58122AD6");
}

TEST(xex_rejects_other_magic) {
  CHECK(!gow3::setup::ReadXexInfo(std::vector<uint8_t>(0x200, 0)));
}

TEST(xex_rejects_bad_offset) {
  auto x = MakeXex(1, 2);
  x[0x1C] = 0xFF;  // execution-info offset now points far past the buffer
  CHECK(!gow3::setup::ReadXexInfo(x));
  CHECK(!gow3::setup::ReadXexInfo(std::vector<uint8_t>{'X', 'E', 'X', '2'}));
}

TEST(xex_hex8_pads) { CHECK(gow3::setup::Hex8(0xAB) == "000000AB"); }

TEST(xex_real_file) {  // skipped unless GOW3_TEST_GAME is set to an extracted game folder
  if (auto g = EnvPath("GOW3_TEST_GAME")) {
    auto i = gow3::setup::ReadXexInfoFile(*g / "default.xex");
    CHECK(i && i->title_id == 0x4D5308AB && i->media_id == 0x58122AD6);
  }
}

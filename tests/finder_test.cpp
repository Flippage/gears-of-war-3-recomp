#include <windows.h>

#include <algorithm>

#include "setup/finder.h"
#include "tests/test_util.h"

namespace fs = std::filesystem;

TEST(finder_game_folder_in_subfolder) {
  auto root = TempDir("find1");
  MakeFakeGame(root / "Gears of War 3");
  auto r = gow3::setup::FindGame({root});
  CHECK(r.game == root / "Gears of War 3");
}

TEST(finder_no_game) {
  auto root = TempDir("find1b");
  fs::create_directories(root / "GearGame");  // no default.xex
  CHECK(!gow3::setup::FindGame({root}).game);
}

TEST(finder_search_roots) {
  auto roots = gow3::setup::SearchRoots(fs::path("C:\\a\\b\\c"), 2);
  CHECK(roots.size() == 3);
  CHECK(roots[0] == fs::path("C:\\a\\b\\c") && roots[2] == fs::path("C:\\a"));
  CHECK(gow3::setup::SearchRoots(fs::path("C:\\"), 2).size() == 1);
}

TEST(finder_iso_title_check) {
  auto root = TempDir("find2");
  WriteFile(root / "Gears.ISO", MakeXiso(0, {{"default.xex", MakeXex(0x58122AD6, 0x4D5308AB)}}));
  WriteFile(root / "Other.iso", MakeXiso(0, {{"default.xex", MakeXex(1, 0x12345678)}}));
  WriteFile(root / "junk.iso", Bytes(0x2000));
  auto r = gow3::setup::FindGame({root});
  CHECK(r.isos.size() == 1 && r.isos[0].xex.media_id == 0x58122AD6);
  CHECK(r.isos.size() == 1 && r.isos[0].file.filename() == "Gears.ISO");
}

TEST(finder_packages_classify) {
  auto root = TempDir("find3");
  WriteFile(root / "TU A" / "TU_16L625B_000001C000000.00000000001GB", MakeStfsHeader("LIVE", 0x000B0000, 0x4D5308AB, 0x58122AD6, u"TU6"));
  WriteFile(root / "TU B" / "TU_other", MakeStfsHeader("LIVE", 0x000B0000, 0x4D5308AB, 0x6EE9A50B, u"TU6"));
  WriteFile(root / "DLC" / "raam", MakeStfsHeader("LIVE", 0x00000002, 0x4D5308AB, 0, u"RAAM's Shadow"));
  WriteFile(root / "big.iso", Bytes(0x2000));
  WriteFile(root / "other_title", MakeStfsHeader("LIVE", 0x00000002, 0x12345678, 0, u"Other"));
  auto p = gow3::setup::FindPackages({root, root / "DLC"}, 0x58122AD6);  // overlapping roots
  CHECK(p.size() == 3);
  CHECK(std::ranges::count_if(p, [](auto& x) { return x.is_tu && x.matches_disc; }) == 1);
  CHECK(std::ranges::count_if(p, [](auto& x) { return x.is_tu && !x.matches_disc; }) == 1);
  CHECK(std::ranges::count_if(p, [](auto& x) { return !x.is_tu && x.matches_disc; }) == 1);
}

TEST(finder_locked_file_skipped) {
  auto root = TempDir("find4");
  WriteFile(root / "pkg", MakeStfsHeader("LIVE", 0x00000002, 0x4D5308AB, 0, u"X"));
  HANDLE h = CreateFileW((root / "pkg").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  CHECK(gow3::setup::FindPackages({root}, 0).empty());
  CloseHandle(h);
}

TEST(finder_real_game_from_parent) {
  auto game = EnvPath("GOW3_TEST_GAME");
  if (!game) return;
  auto r = gow3::setup::FindGame({game->parent_path()});
  CHECK(r.game == *game);
}

TEST(finder_game_found_skips_isos) {
  auto root = TempDir("find5");
  MakeFakeGame(root / "Game");
  WriteFile(root / "Gears.iso", MakeXiso(0, {{"default.xex", MakeXex(0x58122AD6, 0x4D5308AB)}}));
  auto r = gow3::setup::FindGame({root});
  CHECK(r.game == root / "Game");
  CHECK(r.isos.empty());
}

TEST(finder_package_skip_list_and_size_floor) {
  auto root = TempDir("find6");
  auto hdr = MakeStfsHeader("LIVE", 0x00000002, 0x4D5308AB, 0, u"X");
  WriteFile(root / "x.zip", hdr);
  WriteFile(root / "y.ZIP", hdr);
  auto short_file = hdr;
  short_file.resize(0xFFF);
  WriteFile(root / "short", short_file);
  WriteFile(root / "exact", hdr);
  auto p = gow3::setup::FindPackages({root}, 0);
  CHECK(p.size() == 1 && p[0].file.filename() == "exact");
}

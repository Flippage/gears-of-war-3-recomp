#include <windows.h>

#include <algorithm>
#include <chrono>
#include <string>

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

TEST(finder_package_roots) {
  const auto roots = gow3::setup::PackageRoots(fs::path("C:\\r\\a\\b\\app"), fs::path("D:\\Games\\Gears of War 3"));
  CHECK(roots.size() == 5);
  CHECK(roots.size() == 5 && roots[0] == fs::path("C:\\r\\a\\b\\app") && roots[1] == fs::path("C:\\r\\a\\b") &&
        roots[2] == fs::path("C:\\r\\a") && roots[3] == fs::path("D:\\Games\\Gears of War 3") &&
        roots[4] == fs::path("D:\\Games"));
  CHECK(gow3::setup::PackageRoots(fs::path("C:\\app"), fs::path()).size() == 2);  // no game: launcher side only
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

TEST(finder_packages_skip_cloud_only_files) {
  auto root = TempDir("find_cloud");
  WriteFile(root / "DLC" / "raam", MakeStfsHeader("LIVE", 0x00000002, 0x4D5308AB, 0, u"RAAM's Shadow"));
  WriteFile(root / "DLC" / "pack", MakeStfsHeader("LIVE", 0x00000002, 0x4D5308AB, 0, u"Booster Map Pack"));
  // OFFLINE is how cloud-only files (OneDrive placeholders) show up: reading one downloads it.
  CHECK(SetFileAttributesW((root / "DLC" / "pack").c_str(), FILE_ATTRIBUTE_OFFLINE));
  auto p = gow3::setup::FindPackages({root}, 0);
  SetFileAttributesW((root / "DLC" / "pack").c_str(), FILE_ATTRIBUTE_NORMAL);
  CHECK(p.size() == 1 && p[0].header.display_name == "RAAM's Shadow");
}

TEST(finder_packages_budget_stops_the_search) {
  auto root = TempDir("find_budget");
  for (int i = 0; i < 200; ++i) WriteFile(root / ("d" + std::to_string(i)) / "f.txt", "x");
  WriteFile(root / "zz" / "raam", MakeStfsHeader("LIVE", 0x00000002, 0x4D5308AB, 0, u"RAAM's Shadow"));
  const auto t0 = std::chrono::steady_clock::now();
  gow3::setup::FindPackages({root}, 0, std::chrono::milliseconds(1));
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
  CHECK(gow3::setup::FindPackages({root}, 0).size() == 1);  // no budget: the whole tree
}

TEST(finder_packages_xbox_layout_and_skips) {
  auto root = TempDir("find_layout");
  // <name>\4D5308AB\00000002\<package> (4 folders below the root) and a TU two folders down.
  WriteFile(root / "Gears3-DLC" / "RAAM's Shadow" / "4D5308AB" / "00000002" / "354B60",
            MakeStfsHeader("LIVE", 0x00000002, 0x4D5308AB, 0, u"RAAM's Shadow"));
  WriteFile(root / "Gears3-TU6" / "Title Update 6" / "TU_16L625B",
            MakeStfsHeader("LIVE", 0x000B0000, 0x4D5308AB, 0x58122AD6, u"TU6"));
  // Too deep, the disc's own avatar awards, and a skipped game data folder.
  WriteFile(root / "a" / "b" / "c" / "d" / "e" / "deep", MakeStfsHeader("LIVE", 0x00000002, 0x4D5308AB, 0, u"Deep"));
  WriteFile(root / "Game" / "DLC" / "AvatarAwards", MakeStfsHeader("PIRS", 0x00000002, 0x4D5308AB, 0, u"Gears of War 3"));
  WriteFile(root / "Game" / "GearGame" / "pkg", MakeStfsHeader("LIVE", 0x00000002, 0x4D5308AB, 0, u"Data"));
  auto p = gow3::setup::FindPackages({root}, 0x58122AD6);
  CHECK(p.size() == 2);
  CHECK(std::ranges::count_if(p, [](auto& x) { return x.is_tu && x.matches_disc; }) == 1);
  CHECK(std::ranges::count_if(p, [](auto& x) { return x.header.display_name == "RAAM's Shadow"; }) == 1);
  // Picked by the player: a file, or a folder searched like a root.
  CHECK(gow3::setup::FindPackagesAt(root / "Gears3-TU6" / "Title Update 6" / "TU_16L625B", 0x58122AD6).size() == 1);
  CHECK(gow3::setup::FindPackagesAt(root / "a", 0).size() == 1);
  CHECK(gow3::setup::FindPackagesAt(root / "Game" / "DLC" / "AvatarAwards", 0).empty());
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

TEST(finder_extracting_marker) {
  auto root = TempDir("find7");
  MakeFakeGame(root / "Game");
  CHECK(gow3::setup::IsGameFolder(root / "Game"));
  WriteFile(root / "Game" / gow3::setup::kExtractingMarker, "");
  CHECK(!gow3::setup::IsGameFolder(root / "Game"));
  CHECK(!gow3::setup::FindGame({root}).game);
  fs::remove(root / "Game" / gow3::setup::kExtractingMarker);
  CHECK(gow3::setup::IsGameFolder(root / "Game"));
}

TEST(finder_resolve_game_folder) {
  using gow3::setup::ResolveGameFolder;
  auto root = TempDir("resolve");
  const fs::path exe_dir = root / "a" / "b" / "release";
  fs::create_directories(exe_dir);
  // Nothing anywhere: the given folder comes back as it is.
  CHECK(ResolveGameFolder(root / "nope", exe_dir) == root / "nope");
  // Two parents up, in a subfolder.
  MakeFakeGame(root / "a" / "Gears of War 3");
  CHECK(ResolveGameFolder({}, exe_dir) == root / "a" / "Gears of War 3");
  // The path file wins over the search; it may name the game's parent.
  MakeFakeGame(root / "elsewhere" / "G3");
  CHECK(gow3::setup::WriteGamePathFile(exe_dir / "gow3_game_path.txt", root / "elsewhere"));
  CHECK(ResolveGameFolder({}, exe_dir) == root / "elsewhere" / "G3");
  // A stale path file falls through to the search.
  CHECK(gow3::setup::WriteGamePathFile(exe_dir / "gow3_game_path.txt", root / "gone"));
  CHECK(ResolveGameFolder({}, exe_dir) == root / "a" / "Gears of War 3");
  // A valid --game_data_root wins over both.
  CHECK(ResolveGameFolder(root / "elsewhere" / "G3", exe_dir) == root / "elsewhere" / "G3");
  // Five levels up is too far.
  const fs::path deep = root / "a" / "1" / "2" / "3" / "4" / "5";
  fs::create_directories(deep);
  CHECK(ResolveGameFolder({}, deep).empty());
}

TEST(finder_game_data_root_arg) {
  using gow3::setup::GameDataRootArg;
  CHECK(GameDataRootArg(L"").empty());
  CHECK(GameDataRootArg(L"--fullscreen=false").empty());
  CHECK(GameDataRootArg(L"--game_data_root=C:\\G3") == fs::path(L"C:\\G3"));
  CHECK(GameDataRootArg(L"--x=1 --game_data_root=\"D:\\Spiele\\Gears of War 3\" --y") ==
        fs::path(L"D:\\Spiele\\Gears of War 3"));
  CHECK(GameDataRootArg(L"\"--game_data_root=D:\\My Games\\G3\"") == fs::path(L"D:\\My Games\\G3"));
  CHECK(GameDataRootArg(L"--game_data_root \"E:\\G 3\"") == fs::path(L"E:\\G 3"));
  // A trailing backslash before the closing quote: split as gow3.exe's own command line is.
  CHECK(GameDataRootArg(L"--game_data_root=\"C:\\G3\\\\\" --y") == fs::path(L"C:\\G3\\"));
  CHECK(GameDataRootArg(L"--game_data_root=\"C:\\G3\\\"") == fs::path(L"C:\\G3\""));
  CHECK(GameDataRootArg(L"--game_data_root_x=C:\\G3").empty());
}

TEST(finder_title_update_folder) {
  using gow3::setup::FindTitleUpdateFolderIn;
  auto root = TempDir("tu6find");
  const fs::path game = root / "game", exe = root / "release", own = root / "mine";
  fs::create_directories(game);
  fs::create_directories(exe);
  CHECK(!FindTitleUpdateFolderIn(game, exe, {}));
  WriteFile(game / "TU6" / "default.xexp", "x");
  CHECK(FindTitleUpdateFolderIn(game, exe, {}) == game / "TU6");
  WriteFile(exe / "TU6" / "default.xexp", "x");
  CHECK(FindTitleUpdateFolderIn(game, exe, {}) == exe / "TU6");
  // gow3_tu6_folder set: only that folder counts.
  CHECK(!FindTitleUpdateFolderIn(game, exe, own));
  WriteFile(own / "default.xexp", "x");
  CHECK(FindTitleUpdateFolderIn(game, exe, own) == own);
  CHECK(gow3::setup::TitleUpdateCandidates(game, exe, {}).size() == 2);
}

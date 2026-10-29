#include "setup/state.h"
#include "test_util.h"

TEST(state_round_trip) {
  auto dir = TempDir("state");
  gow3::setup::LauncherState s; s.build = gow3::setup::Build::kTu6;
  s.declined = {"a:1", "b:2"}; s.skip_release = "v0.3.1"; s.check_updates = false; s.last_check = 1793000000;
  s.content_asked = true;
  CHECK(gow3::setup::SaveState(dir / "launcher.ini", s));
  auto r = gow3::setup::LoadState(dir / "launcher.ini");
  CHECK(r.existed && r.build == gow3::setup::Build::kTu6 && r.declined == s.declined);
  CHECK(r.skip_release == "v0.3.1" && !r.check_updates && r.last_check == 1793000000 && r.content_asked);
}
TEST(state_missing_is_base) {
  auto r = gow3::setup::LoadState(TempDir("state2") / "launcher.ini");
  CHECK(!r.existed && r.build == gow3::setup::Build::kBase && r.check_updates);
}
TEST(state_format_and_overwrite) {
  auto dir = TempDir("state3");
  gow3::setup::LauncherState s; s.declined = {"a:1", "b:2"}; s.installed = {"c:3"};
  CHECK(gow3::setup::SaveState(dir / "launcher.ini", s));
  CHECK(gow3::setup::SaveState(dir / "launcher.ini", s));  // replaces existing
  CHECK(FileText(dir / "launcher.ini") ==
        "build = base\ndeclined = a:1|b:2\ninstalled = c:3\nskip_release = \ncheck_updates = true\ncontent_asked = false\nlast_check = 0\n");
  CHECK(!std::filesystem::exists(dir / "launcher.ini.tmp"));
  WriteFile(dir / "x.ini", "junk\nbuild = tu6\nfoo = bar\n");
  auto r = gow3::setup::LoadState(dir / "x.ini");
  CHECK(r.existed && r.build == gow3::setup::Build::kTu6 && r.declined.empty());
}
TEST(state_ids_with_comma) {
  auto dir = TempDir("state4");
  gow3::setup::LauncherState s;
  s.installed = {"RAAM's Shadow, part 1:123", "x:1"};
  s.declined = {"Fenix, Marcus:7"};
  CHECK(gow3::setup::SaveState(dir / "launcher.ini", s));
  auto r = gow3::setup::LoadState(dir / "launcher.ini");
  CHECK(r.installed == s.installed && r.declined == s.declined);
  CHECK(r.installed.count("RAAM's Shadow, part 1:123") == 1);
}
TEST(game_path_non_ascii) {
  auto dir = TempDir("path");
  const auto game = dir / L"Gears of War 3 \u2013 Kopie \u0418\u0433\u0440\u0430";
  CHECK(gow3::setup::WriteGamePathFile(dir / "gow3_game_path.txt", game));
  CHECK(gow3::setup::ReadGamePathFile(dir / "gow3_game_path.txt") == game);
  WriteFile(dir / "crlf.txt", "C:\\Games\\G3  \r\nsecond line\r\n");
  CHECK(gow3::setup::ReadGamePathFile(dir / "crlf.txt") == std::filesystem::path("C:\\Games\\G3"));
  CHECK(gow3::setup::ReadGamePathFile(dir / "none.txt").empty());
  WriteFile(dir / "bom.txt", "\xEF\xBB\xBF" "C:\\Games\\G3\n");
  CHECK(gow3::setup::ReadGamePathFile(dir / "bom.txt") == std::filesystem::path("C:\\Games\\G3"));
}
TEST(version_compare) {
  CHECK(gow3::setup::IsNewer("v0.3.0", "0.2.2"));
  CHECK(!gow3::setup::IsNewer("v0.2.2", "0.2.2"));
  CHECK(gow3::setup::IsNewer("v0.10.0", "0.9.9"));
  CHECK(!gow3::setup::IsNewer("nightly", "0.2.2"));
  CHECK(gow3::setup::ParseVersion("0.3") == (std::array<int, 3>{0, 3, 0}));
  CHECK(!gow3::setup::ParseVersion("0.3.0-test"));
  CHECK(!gow3::setup::ParseVersion("0..1"));
  CHECK(!gow3::setup::ParseVersion("1.2.3.4"));
  CHECK(!gow3::setup::ParseVersion("-1.2"));
  CHECK(!gow3::setup::ParseVersion("0.-3"));
  CHECK(!gow3::setup::ParseVersion("+1.2"));
  CHECK(!gow3::setup::ParseVersion("1"));
  CHECK(!gow3::setup::ParseVersion(""));
}
TEST(package_id) {
  CHECK(gow3::setup::PackageId("C:\\x\\TU_16L625B_000001C000000.00000000001GB", 42) ==
        "TU_16L625B_000001C000000.00000000001GB:42");
}

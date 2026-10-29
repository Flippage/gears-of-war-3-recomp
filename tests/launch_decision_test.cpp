#include "setup/launch_decision.h"
#include "tests/test_util.h"

TEST(decide_order) {
  using namespace gow3::setup;
  LauncherState st;
  std::vector<FoundPackage> none, dlc{FakeDlc("raam", 10)}, tu{FakeTu("tu", 20, true)};
  CHECK(DecideLaunch({Build::kBase, false, false, &dlc, &st}) == LaunchAction::kSetup);
  CHECK(DecideLaunch({Build::kBase, true, false, &tu, &st}) == LaunchAction::kOffer);
  CHECK(DecideLaunch({Build::kTu6, true, false, &none, &st}) == LaunchAction::kRepair);
  CHECK(DecideLaunch({Build::kTu6, true, true, &dlc, &st}) == LaunchAction::kOffer);
  CHECK(DecideLaunch({Build::kTu6, true, true, &tu, &st}) == LaunchAction::kStartGame);
  st.declined.insert(dlc[0].id);
  CHECK(DecideLaunch({Build::kBase, true, false, &dlc, &st}) == LaunchAction::kStartGame);
}

TEST(new_packages_skip_other_disc_tu) {
  gow3::setup::LauncherState st;
  CHECK(gow3::setup::NewPackages({FakeTu("tu", 20, false)}, st, gow3::setup::Build::kBase).empty());
}

TEST(new_packages_skip_installed_and_tu_on_tu6) {
  using namespace gow3::setup;
  LauncherState st;
  std::vector<FoundPackage> v{FakeDlc("a", 1), FakeDlc("b", 2), FakeTu("tu", 3, true)};
  st.installed.insert(v[0].id);
  auto n = NewPackages(v, st, Build::kBase);
  CHECK(n.size() == 2 && n[0] == &v[1] && n[1] == &v[2]);
  n = NewPackages(v, st, Build::kTu6);
  CHECK(n.size() == 1 && n[0] == &v[1]);
}

TEST(new_packages_build_decides_tu) {
  using namespace gow3::setup;
  LauncherState st;
  std::vector<FoundPackage> v{FakeTu("tu", 3, true)};
  CHECK(NewPackages(v, st, Build::kBase).size() == 1);
  CHECK(NewPackages(v, st, Build::kTu6).empty());
}

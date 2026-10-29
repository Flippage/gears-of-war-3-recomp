#include "setup/install.h"
#include "tests/test_util.h"

using namespace std::chrono_literals;
namespace fs = std::filesystem;

TEST(swap_build_replaces_and_keeps_on_missing) {
  auto d = TempDir("swap");
  WriteFile(d / "builds" / "gow3-tu6.exe", "TU6");
  WriteFile(d / "gow3.exe", "BASE");
  CHECK(gow3::setup::SwapBuild(d, gow3::setup::Build::kTu6, 1s) == gow3::setup::SwapResult::kOk);
  CHECK(FileText(d / "gow3.exe") == "TU6" && !fs::exists(d / "gow3.exe.new"));
  fs::remove(d / "builds" / "gow3-tu6.exe");
  CHECK(gow3::setup::SwapBuild(d, gow3::setup::Build::kTu6, 1s) == gow3::setup::SwapResult::kFailed);
  CHECK(FileText(d / "gow3.exe") == "TU6" && !fs::exists(d / "gow3.exe.new"));
}

TEST(swap_build_waits_for_running_game) {
  auto d = TempDir("swap2");
  fs::create_directories(d);
  fs::copy_file(SystemExe("PING.EXE"), d / "gow3.exe");
  WriteFile(d / "builds" / "gow3-base.exe", "BASE");
  TestProcess proc = StartProcess(d / "gow3.exe", L"-n 6 127.0.0.1");  // killed on scope exit too
  CHECK(proc.handle != nullptr);
  CHECK(gow3::setup::IsGameRunning(d / "gow3.exe"));
  CHECK(gow3::setup::SwapBuild(d, gow3::setup::Build::kBase, 1s) == gow3::setup::SwapResult::kGameRunning);
  CHECK(FileBytes(d / "gow3.exe") == FileBytes(SystemExe("PING.EXE")));
  KillProcess(proc);
}

TEST(is_game_running_false_for_other_file) {
  auto d = TempDir("swap3");
  WriteFile(d / "gow3.exe", "x");
  CHECK(!gow3::setup::IsGameRunning(d / "gow3.exe"));
}

TEST(install_dlc_copies_and_overwrites) {
  auto d = TempDir("dlc");
  auto dlc = FakeDlc("raam", 4);
  dlc.file = d / "src" / "raam";
  WriteFile(dlc.file, "DATA");
  auto game = d / "game";
  std::string err;
  CHECK(gow3::setup::InstallDlc(dlc, game, &err));
  CHECK(FileText(game / "DLC" / "raam") == "DATA" && fs::exists(dlc.file));
  WriteFile(game / "DLC" / "raam", "OLD");
  CHECK(gow3::setup::InstallDlc(dlc, game, &err));
  CHECK(FileText(game / "DLC" / "raam") == "DATA" && fs::exists(dlc.file));
  dlc.file = d / "src" / "missing";
  CHECK(!gow3::setup::InstallDlc(dlc, game, &err) && !err.empty());
}

static auto TuFile(const fs::path& path, const std::string& xexp) {
  WriteFile(path, MakeStfsPackage({{"default.xexp", xexp}, {"other.bin", "OTHER"}}));
  auto tu = FakeTu(path.filename().string(), 1, true);
  tu.file = path;
  return tu;
}

TEST(install_tu_extracts_replaces_and_keeps_on_failure) {
  auto d = TempDir("tu");
  auto game = d / "game";
  auto progress = [](uint64_t, uint64_t, const std::string&) { return true; };
  std::string err;
  auto tu1 = TuFile(d / "tu1", "FIRST");
  CHECK(gow3::setup::InstallTitleUpdate(tu1, game, progress, &err) == gow3::setup::ExtractResult::kOk);
  CHECK(FileText(game / "TU6" / "default.xexp") == "FIRST" && FileText(game / "TU6" / "other.bin") == "OTHER");
  CHECK(!fs::exists(game / "TU6.part-dir") && !fs::exists(game / "TU6.old"));

  auto tu2 = TuFile(d / "tu2", "SECOND");
  CHECK(gow3::setup::InstallTitleUpdate(tu2, game, progress, &err) == gow3::setup::ExtractResult::kOk);
  CHECK(FileText(game / "TU6" / "default.xexp") == "SECOND");
  CHECK(!fs::exists(game / "TU6.part-dir") && !fs::exists(game / "TU6.old"));

  // Failure (not a package): the existing TU6 stays as it was.
  auto bad = FakeTu("bad", 1, true);
  bad.file = d / "bad";
  WriteFile(bad.file, "not a package");
  err.clear();
  CHECK(gow3::setup::InstallTitleUpdate(bad, game, progress, &err) == gow3::setup::ExtractResult::kFailed);
  CHECK(!err.empty());
  CHECK(FileText(game / "TU6" / "default.xexp") == "SECOND" && !fs::exists(game / "TU6.part-dir"));

  // Cancel: also leaves it alone.
  auto cancel = [](uint64_t, uint64_t, const std::string&) { return false; };
  CHECK(gow3::setup::InstallTitleUpdate(tu1, game, cancel, &err) == gow3::setup::ExtractResult::kCancelled);
  CHECK(FileText(game / "TU6" / "default.xexp") == "SECOND" && !fs::exists(game / "TU6.part-dir"));
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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

// A directory handle held open inside TU6.part-dir (no FILE_SHARE_DELETE) blocks the final rename.
// The old TU6 is then restored, so it must come back intact. Held via a progress callback on the
// first file, which runs after the part dir exists.
TEST(install_tu_final_rename_failure_restores_old) {
  auto d = TempDir("tu_rename");
  auto game = d / "game";
  std::string err;
  auto tu1 = TuFile(d / "tu1", "FIRST");
  auto progress = [](uint64_t, uint64_t, const std::string&) { return true; };
  CHECK(gow3::setup::InstallTitleUpdate(tu1, game, progress, &err) == gow3::setup::ExtractResult::kOk);
  auto tu2 = TuFile(d / "tu2", "SECOND");
  HANDLE h = INVALID_HANDLE_VALUE;
  auto hold = [&](uint64_t, uint64_t, const std::string&) {
    if (h == INVALID_HANDLE_VALUE)
      h = CreateFileW((game / "TU6.part-dir").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    return true;
  };
  auto r = gow3::setup::InstallTitleUpdate(tu2, game, hold, &err);
  bool held = h != INVALID_HANDLE_VALUE;
  if (held) CloseHandle(h);
  if (!held || r == gow3::setup::ExtractResult::kOk) {
    std::printf("skip: could not block the final rename on this system\n");
    return;
  }
  CHECK(r == gow3::setup::ExtractResult::kFailed && !err.empty());
  CHECK(FileText(game / "TU6" / "default.xexp") == "FIRST" && !fs::exists(game / "TU6.old"));
}

TEST(swap_build_clears_read_only) {
  auto d = TempDir("swap_ro");
  WriteFile(d / "builds" / "gow3-tu6.exe", "TU6");
  WriteFile(d / "builds" / "gow3-base.exe", "BASE");
  WriteFile(d / "gow3.exe", "OLD");
  const fs::path tu6 = d / "builds" / "gow3-tu6.exe";
  SetFileAttributesW(tu6.c_str(), FILE_ATTRIBUTE_READONLY);
  CHECK(gow3::setup::SwapBuild(d, gow3::setup::Build::kTu6, 1s) == gow3::setup::SwapResult::kOk);
  CHECK(!(GetFileAttributesW((d / "gow3.exe").c_str()) & FILE_ATTRIBUTE_READONLY));
  CHECK(gow3::setup::SwapBuild(d, gow3::setup::Build::kBase, 1s) == gow3::setup::SwapResult::kOk);
  CHECK(FileText(d / "gow3.exe") == "BASE");
  SetFileAttributesW(tu6.c_str(), FILE_ATTRIBUTE_NORMAL);  // so TempDir can empty it next run
}

TEST(wait_for_game_exit) {
  auto d = TempDir("wait");
  WriteFile(d / "gow3.exe", "x");
  CHECK(gow3::setup::WaitForGameExit(d / "gow3.exe", 0ms) == gow3::setup::WaitResult::kExited);
  fs::create_directories(d / "ping");
  fs::copy_file(SystemExe("PING.EXE"), d / "ping" / "gow3.exe");
  TestProcess proc = StartProcess(d / "ping" / "gow3.exe", L"-n 6 127.0.0.1");
  CHECK(gow3::setup::WaitForGameExit(d / "ping" / "gow3.exe", 300ms) == gow3::setup::WaitResult::kTimedOut);
  CHECK(gow3::setup::WaitForGameExit(d / "ping" / "gow3.exe", 10s, [] { return true; }) ==
        gow3::setup::WaitResult::kCancelled);
  KillProcess(proc);
}
TEST(same_file_contents) {
  auto d = TempDir("same");
  WriteFile(d / "a", "BASE BUILD");
  WriteFile(d / "b", "BASE BUILD");
  WriteFile(d / "c", "TU6 BUILD!");  // same size, other bytes
  WriteFile(d / "e", "TU6");
  CHECK(gow3::setup::SameFileContents(d / "a", d / "b"));
  CHECK(!gow3::setup::SameFileContents(d / "a", d / "c"));
  CHECK(!gow3::setup::SameFileContents(d / "a", d / "e"));
  CHECK(!gow3::setup::SameFileContents(d / "a", d / "missing"));
  auto big = Bytes(3 << 20);
  WriteFile(d / "big1", big);
  big.back() ^= 1;
  WriteFile(d / "big2", big);
  CHECK(!gow3::setup::SameFileContents(d / "big1", d / "big2"));
  CHECK(gow3::setup::SameFileContents(d / "big1", d / "big1"));
}
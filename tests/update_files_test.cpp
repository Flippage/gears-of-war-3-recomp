#include <algorithm>

#include "setup/update_files.h"
#include "test_util.h"

namespace fs = std::filesystem;

TEST(sha256_file_parse) {
  using gow3::setup::ParseSha256File;
  const std::string lower(64, 'a');
  CHECK(ParseSha256File(lower + "  gow3-recomp-0.3.0-win64.zip\r\n") == lower);
  CHECK(ParseSha256File("  " + lower + "\n") == lower);
  // package.ps1 writes Get-FileHash's upper case.
  CHECK(ParseSha256File("F264F66B86A699F1B76F7713999C2F57B5A05D8A64D2F709BD2CD523C540BBEB  x.zip") ==
        std::string("f264f66b86a699f1b76f7713999c2f57b5a05d8a64d2f709bd2cd523c540bbeb"));
  CHECK(!ParseSha256File(std::string(63, 'a') + "  x.zip"));  // too short
  CHECK(!ParseSha256File(std::string(65, 'a') + "  x.zip"));  // too long
  CHECK(!ParseSha256File(std::string(63, 'a') + "g  x.zip"));  // not hex
  CHECK(!ParseSha256File(""));
  CHECK(!ParseSha256File(" \r\n"));
}

TEST(update_unsafe_entry) {
  using gow3::setup::UnsafeEntry;
  CHECK(!UnsafeEntry("gow3-recomp-9.9.9-win64/\r\ngow3-recomp-9.9.9-win64/launcher.exe\r\n\r\n"));
  CHECK(!UnsafeEntry(""));
  CHECK(UnsafeEntry("top/a.txt\ntop/../../evil.txt\ntop/b.txt\n") == std::string("top/../../evil.txt"));
  CHECK(UnsafeEntry("top/a.txt\n/abs.txt\n") == std::string("/abs.txt"));
  CHECK(UnsafeEntry("top\\..\\evil.txt\r\n") == std::string("top\\..\\evil.txt"));
  CHECK(UnsafeEntry("C:evil.txt\n") == std::string("C:evil.txt"));
}

TEST(update_files_to_copy) {
  const fs::path src = TempDir("update_files") / "gow3-recomp-9.9.9-win64";
  for (const char* f : {"launcher.exe", "gow3.exe", "gow3.toml", "GOW3_GAME_PATH.TXT", "launcher.ini",
                        "launcher.old.exe", "version.txt", "rexruntime.dll", "builds/gow3-base.exe",
                        "builds/gow3-tu6.exe", "builds/gow3.exe", "assets/gow3.toml", "update.tmp/x.txt"}) {
    fs::create_directories((src / f).parent_path());
    WriteFile(src / f, "x");
  }
  std::vector<fs::path> files = gow3::setup::FilesToCopy(src);
  std::vector<std::string> names;
  for (const fs::path& p : files) names.push_back(p.generic_string());
  std::sort(names.begin(), names.end());
  // Kept files, the top-level gow3.exe (SwapBuild makes it), version.txt (copied after the swap),
  // launcher.old.exe and update.tmp are left out; nested files of the same names are copied.
  CHECK(names == std::vector<std::string>({"assets/gow3.toml", "builds/gow3-base.exe", "builds/gow3-tu6.exe",
                                           "builds/gow3.exe", "launcher.exe", "rexruntime.dll"}));
  CHECK(gow3::setup::FilesToCopy(src / "missing").empty());
}

TEST(update_backup_pending) {
  using gow3::setup::RemoveStaleUpdateTmp;
  using gow3::setup::UpdateBackupPending;
  const fs::path dir = TempDir("update_pending");
  CHECK(!UpdateBackupPending(dir));
  RemoveStaleUpdateTmp(dir);  // nothing there: fine
  // Left by a finished update (replaced DLLs that were loaded, a download): removed.
  WriteFile(dir / "update.tmp" / "backup" / "rexruntime.dll", "old");
  WriteFile(dir / "update.tmp" / "x.zip", "zip");
  CHECK(!UpdateBackupPending(dir));
  RemoveStaleUpdateTmp(dir);
  CHECK(!fs::exists(dir / "update.tmp"));
  // A failed rollback's originals (the marker): kept.
  WriteFile(dir / "update.tmp" / "backup" / "builds" / "gow3-tu6.exe", "old");
  WriteFile(dir / "update.tmp" / gow3::setup::kRollbackFailedMarker, "builds\\gow3-tu6.exe\n");
  CHECK(UpdateBackupPending(dir));
  RemoveStaleUpdateTmp(dir);
  CHECK(fs::exists(dir / "update.tmp" / "backup" / "builds" / "gow3-tu6.exe"));
}

TEST(update_copies_default_toml) {
  const fs::path src = TempDir("update_files2") / "gow3-recomp-9.9.9-win64";
  WriteFile(src / "gow3.toml", "x");
  WriteFile(src / "config" / "gow3.default.toml", "x");
  const std::vector<fs::path> files = gow3::setup::FilesToCopy(src);
  // gow3.toml is the player's; the release's default is replaced like any release file.
  CHECK(files.size() == 1 && files[0].generic_string() == "config/gow3.default.toml");
}
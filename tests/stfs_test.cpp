#include "setup/stfs.h"
#include "test_util.h"

TEST(stfs_header_fields) {
  auto f = TempDir("stfs") / "pkg";
  WriteFile(f, MakeStfsHeader("LIVE", 0x000B0000, 0x4D5308AB, 0x58122AD6, u"Title Update 6"));
  auto h = gow3::setup::ReadStfsHeader(f);
  CHECK(h && h->magic == "LIVE" && h->content_type == 0x000B0000);
  CHECK(h && h->title_id == 0x4D5308AB && h->media_id == 0x58122AD6 && h->display_name == "Title Update 6");
}

TEST(stfs_header_rejects_non_packages) {
  auto dir = TempDir("stfs2");
  WriteFile(dir / "notes.txt", "hello");
  CHECK(!gow3::setup::ReadStfsHeader(dir / "notes.txt"));
  CHECK(!gow3::setup::ReadStfsHeader(dir / "missing"));
}

TEST(stfs_extract_rejects_non_packages) {
  auto dir = TempDir("stfs3");
  WriteFile(dir / "notes.txt", "hello");
  std::string err;
  CHECK(gow3::setup::ExtractStfs(dir / "notes.txt", dir / "out", {}, &err) == gow3::setup::ExtractResult::kFailed);
  CHECK(!err.empty());
}

TEST(stfs_real_tu_matches_python) {  // GOW3_TEST_TU = the 58122AD6 TU file; GOW3_TEST_TU_REF = Gears3-TU6\extracted
  auto tu = EnvPath("GOW3_TEST_TU");
  auto ref = EnvPath("GOW3_TEST_TU_REF");
  if (!tu || !ref) return;
  auto out = TempDir("stfs_real");
  CHECK(gow3::setup::ExtractStfs(*tu, out, {}, nullptr) == gow3::setup::ExtractResult::kOk);
  CHECK(SameTree(out, *ref));  // same relative paths, same sizes, same bytes
}

TEST(stfs_cancel_leaves_no_part_files) {
  auto tu = EnvPath("GOW3_TEST_TU");
  if (!tu) return;
  auto out = TempDir("stfs_cancel");
  CHECK(gow3::setup::ExtractStfs(*tu, out, [](auto, auto, auto&) { return false; }, nullptr) ==
        gow3::setup::ExtractResult::kCancelled);
  CHECK(!AnyFileEndsWith(out, ".part"));
}

TEST(stfs_cancel_mid_extract_keeps_finished_files) {
  auto tu = EnvPath("GOW3_TEST_TU");
  if (!tu) return;
  auto out = TempDir("stfs_cancel2");
  std::vector<std::string> names;
  CHECK(gow3::setup::ExtractStfs(*tu, out, [&](auto, auto, const std::string& n) {
          names.push_back(n);
          return names.size() < 3;
        }, nullptr) == gow3::setup::ExtractResult::kCancelled);
  CHECK(names.size() == 3);
  if (names.size() == 3) {
    if (names[2] != names[0]) CHECK(std::filesystem::exists(out / names[0]));
    CHECK(!std::filesystem::exists(out / names[2]));  // the file being written is gone
  }
  CHECK(!AnyFileEndsWith(out, ".part"));
}

TEST(stfs_extracts_synthetic_package) {
  auto dir = TempDir("stfs_syn");
  std::string big(5000, 'x');
  for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>('a' + i % 26);
  WriteFile(dir / "pkg", MakeStfsPackage({{"big.bin", big}, {"small.txt", "hello"}}));
  CHECK(gow3::setup::ReadStfsHeader(dir / "pkg").has_value());
  std::string err;
  CHECK(gow3::setup::ExtractStfs(dir / "pkg", dir / "out", {}, &err) == gow3::setup::ExtractResult::kOk);
  CHECK(FileText(dir / "out" / "big.bin") == big);
  CHECK(FileText(dir / "out" / "small.txt") == "hello");
  CHECK(!AnyFileEndsWith(dir / "out", ".part"));
}

TEST(stfs_rejects_unsafe_entry_names) {
  for (const char* bad : {"..\\x","a:b", "/x", "..", "sub/../x"}) {
    auto dir = TempDir("stfs_bad");
    WriteFile(dir / "pkg", MakeStfsPackage({{"ok.txt", "fine"}, {bad, "evil"}}));
    std::string err;
    CHECK(gow3::setup::ExtractStfs(dir / "pkg", dir / "out", {}, &err) == gow3::setup::ExtractResult::kFailed);
    CHECK(!err.empty());
    CHECK(!AnyFileEndsWith(dir / "out", ""));  // nothing written, even the good entry
    CHECK(!std::filesystem::exists(dir / "x"));
  }
}

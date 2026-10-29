#include "setup/xdvdfs.h"

#include "test_util.h"

using gow3::setup::DiscImage;
using gow3::setup::ExtractResult;

TEST(xdvdfs_lists_and_extracts_xgd3) {
  auto dir = TempDir("xdvdfs_xgd3");
  auto iso = dir / L"Gears И.iso";
  WriteFile(iso, MakeXiso(0x2080000, {{"default.xex", Bytes(300)}, {"GearGame\\a.xxx", Bytes(5000)}}));
  std::string err;
  auto img = DiscImage::Open(iso, &err);
  CHECK(img && img->TotalBytes() == 5300);
  if (!img) return;
  CHECK(img->Entries().size() == 3);  // two files + GearGame
  auto head = img->ReadFile("default.xex", 64);
  CHECK(head && head->size() == 64 && *head == Bytes(64));
  auto whole = img->ReadFile("DEFAULT.XEX", 1000);
  CHECK(whole && whole->size() == 300);
  CHECK(!img->ReadFile("missing.bin", 8));
  CHECK(img->ExtractAll(dir / "out", {}, nullptr) == ExtractResult::kOk);
  CHECK(FileBytes(dir / "out" / "GearGame" / "a.xxx") == Bytes(5000));
  CHECK(FileBytes(dir / "out" / "default.xex") == Bytes(300));
  CHECK(!AnyFileEndsWith(dir / "out", ".part"));
  // Running again overwrites.
  CHECK(img->ExtractAll(dir / "out", {}, nullptr) == ExtractResult::kOk);
  CHECK(FileBytes(dir / "out" / "default.xex") == Bytes(300));
}

TEST(xdvdfs_plain_xiso) {
  auto dir = TempDir("xdvdfs_plain");
  WriteFile(dir / "g.iso", MakeXiso(0, {{"default.xex", Bytes(300)}, {"GearGame\\a.xxx", Bytes(5000)}}));
  auto img = DiscImage::Open(dir / "g.iso", nullptr);
  CHECK(img && img->TotalBytes() == 5300);
  if (!img) return;
  CHECK(img->ExtractAll(dir / "out", {}, nullptr) == ExtractResult::kOk);
  CHECK(FileBytes(dir / "out" / "GearGame" / "a.xxx") == Bytes(5000));
}

TEST(xdvdfs_not_a_disc) {
  auto dir = TempDir("xdvdfs_junk");
  WriteFile(dir / "junk.iso", Bytes(0x20000));
  std::string err;
  CHECK(!DiscImage::Open(dir / "junk.iso", &err));
  CHECK(!err.empty());
  CHECK(!DiscImage::Open(dir / "missing.iso", nullptr));
}

TEST(xdvdfs_rejects_unsafe_names) {
  auto dir = TempDir("xdvdfs_unsafe");
  WriteFile(dir / "evil.iso", MakeXiso(0, {{"..\\evil.txt", Bytes(10)}}));
  std::string err;
  CHECK(!DiscImage::Open(dir / "evil.iso", &err));
  CHECK(err.find("unsafe") != std::string::npos);
  WriteFile(dir / "drive.iso", MakeXiso(0, {{"C:evil.txt", Bytes(10)}}));
  CHECK(!DiscImage::Open(dir / "drive.iso", nullptr));
}

TEST(xdvdfs_cancel) {
  auto dir = TempDir("xdvdfs_cancel");
  WriteFile(dir / "g.iso", MakeXiso(0, {{"a.bin", Bytes(100)}, {"big.bin", Bytes(9u << 20)}}));
  auto img = DiscImage::Open(dir / "g.iso", nullptr);
  CHECK(img.has_value());
  if (!img) return;
  CHECK(img->ExtractAll(dir / "out1", [](uint64_t, uint64_t, const std::string&) { return false; }, nullptr) ==
        ExtractResult::kCancelled);
  CHECK(!AnyFileEndsWith(dir / "out1", ".part"));

  // Cancel after the first 4 MiB chunk of big.bin: a.bin stays, big.bin and its .part do not.
  int big_calls = 0;
  uint64_t last_total = 0;
  auto res = img->ExtractAll(dir / "out2", [&](uint64_t, uint64_t total, const std::string& cur) {
    last_total = total;
    return !(cur == "big.bin" && ++big_calls >= 2);
  }, nullptr);
  CHECK(res == ExtractResult::kCancelled);
  CHECK(last_total == 100 + (9u << 20));
  CHECK(FileBytes(dir / "out2" / "a.bin") == Bytes(100));
  CHECK(!std::filesystem::exists(dir / "out2" / "big.bin"));
  CHECK(!AnyFileEndsWith(dir / "out2", ".part"));
}

TEST(xdvdfs_multi_chunk_progress) {
  auto dir = TempDir("xdvdfs_chunks");
  WriteFile(dir / "g.iso", MakeXiso(0, {{"big.bin", Bytes(9u << 20)}}));
  auto img = DiscImage::Open(dir / "g.iso", nullptr);
  CHECK(img.has_value());
  if (!img) return;
  int calls = 0;
  uint64_t last_done = 0;
  CHECK(img->ExtractAll(dir / "out", [&](uint64_t done, uint64_t, const std::string&) {
    ++calls;
    last_done = done;
    return true;
  }, nullptr) == ExtractResult::kOk);
  CHECK(calls == 5);  // file start, 3 chunks, final
  CHECK(last_done == (9u << 20));
  CHECK(FileBytes(dir / "out" / "big.bin") == Bytes(9u << 20));
}

TEST(xdvdfs_real_iso) {
  if (auto iso = EnvPath("GOW3_TEST_ISO")) {
    auto img = DiscImage::Open(*iso, nullptr);
    CHECK(img && img->ReadFile("default.xex", 0x10000));
  }
}

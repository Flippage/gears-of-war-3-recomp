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

namespace {
constexpr size_t kRoot = 33 * 2048;  // MakeXiso(0, ...) puts the root directory at sector 33
void Put16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
  b[off] = static_cast<uint8_t>(v);
  b[off + 1] = static_cast<uint8_t>(v >> 8);
}
void Put32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
  Put16(b, off, static_cast<uint16_t>(v));
  Put16(b, off + 2, static_cast<uint16_t>(v >> 16));
}
}  // namespace

TEST(xdvdfs_empty_directories) {
  auto dir = TempDir("xdvdfs_empty");
  WriteFile(dir / "g.iso", MakeXiso(0, {{"e1\\", {}}, {"e2\\", {}}, {"a.bin", Bytes(10)}}));
  std::string err;
  auto img = DiscImage::Open(dir / "g.iso", &err);
  CHECK(img.has_value());
  if (!img) return;
  CHECK(img->Entries().size() == 3);
  CHECK(img->ExtractAll(dir / "out", {}, nullptr) == ExtractResult::kOk);
  CHECK(std::filesystem::is_directory(dir / "out" / "e1"));
  CHECK(std::filesystem::is_directory(dir / "out" / "e2"));
}

TEST(xdvdfs_entry_cycle_terminates) {
  auto dir = TempDir("xdvdfs_cycle");
  auto iso = MakeXiso(0, {{"a.bin", Bytes(10)}, {"b.bin", Bytes(10)}});
  Put16(iso, kRoot + 20, 5);  // b.bin's left link points at itself
  WriteFile(dir / "g.iso", iso);
  auto img = DiscImage::Open(dir / "g.iso", nullptr);
  CHECK(img && img->Entries().size() == 2);
}

TEST(xdvdfs_entry_offset_past_directory) {
  auto dir = TempDir("xdvdfs_past");
  auto iso = MakeXiso(0, {{"a.bin", Bytes(10)}, {"b.bin", Bytes(10)}});
  Put16(iso, kRoot + 2, 0x7000);  // a.bin's right link leaves the directory
  WriteFile(dir / "g.iso", iso);
  auto img = DiscImage::Open(dir / "g.iso", nullptr);
  CHECK(img && img->Entries().size() == 1);
}

TEST(xdvdfs_directory_loop) {
  auto dir = TempDir("xdvdfs_dirloop");
  auto iso = MakeXiso(0, {{"d\\x.bin", Bytes(10)}});
  Put32(iso, kRoot + 4, 33);  // d points back at the root directory
  WriteFile(dir / "g.iso", iso);
  std::string err;
  CHECK(!DiscImage::Open(dir / "g.iso", &err));
  CHECK(err.find("loop") != std::string::npos);
}

TEST(xdvdfs_multi_sector_directory_with_padding) {
  auto dir = TempDir("xdvdfs_multisector");
  std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
  for (int i = 0; i < 100; ++i) {
    char name[32];
    std::snprintf(name, sizeof(name), "file_%03d.bin", i);
    files.push_back({name, Bytes(1)});
  }
  WriteFile(dir / "g.iso", MakeXiso(0, files));
  auto img = DiscImage::Open(dir / "g.iso", nullptr);
  CHECK(img && img->Entries().size() == 100 && img->TotalBytes() == 100);
  if (img) CHECK(img->ReadFile("file_099.bin", 8).has_value());
}

TEST(xdvdfs_truncated_image) {
  auto dir = TempDir("xdvdfs_trunc");
  auto iso = MakeXiso(0, {{"a.bin", Bytes(5000)}});
  iso.resize(iso.size() - 2500);
  WriteFile(dir / "g.iso", iso);
  std::string err;
  CHECK(!DiscImage::Open(dir / "g.iso", &err));
  CHECK(err.find("truncated") != std::string::npos);
}

TEST(xdvdfs_xgd2_partition) {
  auto dir = TempDir("xdvdfs_xgd2");
  WriteFile(dir / "g.iso", MakeXiso(0xFD90000, {{"default.xex", Bytes(300)}}));
  auto img = DiscImage::Open(dir / "g.iso", nullptr);
  CHECK(img && img->TotalBytes() == 300);
  if (img) CHECK(*img->ReadFile("default.xex", 300) == Bytes(300));
}

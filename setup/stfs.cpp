#include "setup/stfs.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <span>
#include <system_error>
#include <vector>

#include "setup/be.h"

namespace gow3::setup {

namespace fs = std::filesystem;

namespace {
constexpr size_t kHeaderSize = 0x1000;
constexpr uint64_t kBlock = 0x1000;
constexpr uint64_t kHashesPerTable = 0xAA;
constexpr size_t kMaxDepth = 64;  // directory nesting limit
constexpr size_t kChunkBlocks = 64;  // progress / cancel granularity inside a file

bool IsMagic(const std::string& m) { return m == "CON " || m == "LIVE" || m == "PIRS"; }

void AppendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

struct Entry {
  std::string name;  // UTF-8
  bool dir = false;
  uint32_t blocks = 0, start = 0, size = 0;
  int parent = -1;
};

class Stfs {
 public:
  explicit Stfs(std::ifstream& in) : in_(in) {}

  bool Init(std::string* err) {
    std::array<uint8_t, kHeaderSize> h{};
    if (!ReadAt(0, h.data(), h.size())) { *err = "cannot read package header"; return false; }
    std::span<const uint8_t> s(h);
    std::string magic(reinterpret_cast<const char*>(h.data()), 4);
    if (!IsMagic(magic)) { *err = "not an STFS package"; return false; }
    uint32_t header_size = Be32(s, 0x340);
    base_ = (uint64_t{header_size} + 0xFFF) & ~uint64_t{0xFFF};
    // Volume descriptor at 0x379: block separation, file table block count / number.
    const size_t desc = 0x379;
    uint8_t block_sep = h[desc + 2];
    ft_blocks_ = Le16(s, desc + 3);
    in_.clear();
    in_.seekg(0, std::ios::end);
    uint64_t file_size = static_cast<uint64_t>(in_.tellg());
    if (uint64_t{ft_blocks_} * kBlock > file_size) { *err = "file table larger than package"; return false; }
    ft_start_ = Le32(s, desc + 5) & 0xFFFFFF;
    // Packages created read only (LIVE / PIRS) use one hash table per level.
    tables_per_level_ = (block_sep & 1) ? 1 : 2;
    return true;
  }

  // Data block number -> file offset, skipping the hash table blocks interleaved with data:
  // one more table per level for every 170^(level+1) blocks (Xenia's BlockToOffsetSTFS).
  uint64_t BlockOffset(uint64_t block) const {
    uint64_t backing = block;
    uint64_t step = kHashesPerTable;
    for (int i = 0; i < 3; ++i) {
      backing += (block + step) / step * tables_per_level_;
      if (block < step) break;
      step *= kHashesPerTable;
    }
    return base_ + backing * kBlock;
  }

  // Next block in the chain: bytes 0x15..0x17 (big endian) of the block's 0x18 byte level-0
  // hash entry (sha1, status, next block).
  bool NextBlock(uint64_t block, uint32_t* next) {
    uint64_t table_block = (block / kHashesPerTable) * (kHashesPerTable + tables_per_level_);
    // Level-1 hash tables sit after every 170 level-0 groups; skip the ones before this block.
    if (block >= kHashesPerTable) {
      table_block += ((block / (kHashesPerTable * kHashesPerTable)) + 1) * tables_per_level_;
    }
    uint64_t off = base_ + table_block * kBlock + (block % kHashesPerTable) * 0x18;
    uint64_t table_off = off & ~(kBlock - 1);
    if (table_off != cached_off_) {
      if (!ReadAt(table_off, cached_.data(), kBlock)) return false;
      cached_off_ = table_off;
    }
    size_t at = static_cast<size_t>(off - table_off) + 0x15;
    *next = (uint32_t{cached_[at]} << 16) | (uint32_t{cached_[at + 1]} << 8) | cached_[at + 2];
    return true;
  }

  bool ReadAt(uint64_t off, uint8_t* dst, size_t n) {
    in_.clear();
    in_.seekg(static_cast<std::streamoff>(off), std::ios::beg);
    in_.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
    return static_cast<size_t>(in_.gcount()) == n;
  }

  bool Entries(std::vector<Entry>* out, std::string* err) {
    std::vector<uint8_t> table(size_t{ft_blocks_} * kBlock);
    uint32_t block = ft_start_;
    for (uint32_t i = 0; i < ft_blocks_; ++i) {
      if (!ReadAt(BlockOffset(block), table.data() + size_t{i} * kBlock, kBlock)) {
        *err = "file table is truncated";
        return false;
      }
      if (!NextBlock(block, &block)) { *err = "file table hash is truncated"; return false; }
    }
    std::span<const uint8_t> t(table);
    for (size_t i = 0; i + 0x40 <= table.size(); i += 0x40) {
      uint8_t flags = table[i + 0x28];
      size_t name_len = flags & 0x3F;
      if (name_len == 0) break;
      Entry e;
      for (size_t k = 0; k < name_len; ++k) AppendUtf8(e.name, table[i + k]);  // latin-1
      e.dir = (flags & 0x80) != 0;
      e.blocks = Le32(t, i + 0x29) & 0xFFFFFF;
      e.start = Le32(t, i + 0x2F) & 0xFFFFFF;
      e.parent = static_cast<int16_t>(Be16(t, i + 0x32));
      e.size = Be32(t, i + 0x34);
      out->push_back(std::move(e));
    }
    return true;
  }

 private:
  std::ifstream& in_;
  uint64_t base_ = 0;
  uint32_t ft_blocks_ = 0, ft_start_ = 0;
  uint64_t tables_per_level_ = 2;
  std::array<uint8_t, kBlock> cached_{};
  uint64_t cached_off_ = ~uint64_t{0};
};

std::string Utf8(const fs::path& p) {
  auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

ExtractResult Fail(std::string* error, const std::string& msg) {
  if (error) *error = msg;
  return ExtractResult::kFailed;
}

// Entry paths may not climb out of the output folder or name another drive.
bool SafeName(const std::string& rel) {
  if (rel.empty() || rel[0] == '/' || rel[0] == '\\') return false;
  return rel.find("..") == std::string::npos && rel.find(':') == std::string::npos;
}
}  // namespace

std::optional<StfsHeader> ReadStfsHeader(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return std::nullopt;
  std::array<uint8_t, kHeaderSize> h{};
  in.read(reinterpret_cast<char*>(h.data()), h.size());
  if (static_cast<size_t>(in.gcount()) != h.size()) return std::nullopt;
  std::span<const uint8_t> s(h);
  StfsHeader out;
  out.magic.assign(reinterpret_cast<const char*>(h.data()), 4);
  if (!IsMagic(out.magic)) return std::nullopt;
  out.content_type = Be32(s, 0x344);
  out.media_id = Be32(s, 0x354);
  out.title_id = Be32(s, 0x360);
  for (size_t i = 0; i < 0x80; i += 2) {
    uint32_t c = Be16(s, 0x411 + i);
    if (c == 0) break;
    if (c >= 0xD800 && c < 0xDC00 && i + 2 < 0x80) {
      uint32_t lo = Be16(s, 0x411 + i + 2);
      if (lo >= 0xDC00 && lo < 0xE000) {
        c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
        i += 2;
      }
    }
    AppendUtf8(out.display_name, c);
  }
  return out;
}

ExtractResult ExtractStfs(const fs::path& package, const fs::path& out_dir, const Progress& progress,
                          std::string* error) {
  std::ifstream in(package, std::ios::binary);
  if (!in) return Fail(error, "cannot open " + Utf8(package));
  Stfs stfs(in);
  std::string err;
  std::vector<Entry> entries;
  if (!stfs.Init(&err) || !stfs.Entries(&entries, &err)) return Fail(error, err);

  // Relative path of each entry: its name under the chain of parent directories.
  std::vector<std::string> paths(entries.size());
  uint64_t total = 0;
  for (size_t i = 0; i < entries.size(); ++i) {
    std::string p = entries[i].name;
    int parent = entries[i].parent;
    for (size_t depth = 0; parent >= 0 && parent < static_cast<int>(entries.size()); ++depth) {
      if (depth > kMaxDepth) return Fail(error, "directory loop in file table");
      p = entries[parent].name + "/" + p;
      parent = entries[parent].parent;
    }
    if (!SafeName(p)) return Fail(error, "unsafe path in package: " + p);
    paths[i] = std::move(p);
    if (!entries[i].dir) total += entries[i].size;
  }

  uint64_t done = 0;
  std::vector<uint8_t> buf(kBlock * kChunkBlocks);
  for (size_t i = 0; i < entries.size(); ++i) {
    const Entry& e = entries[i];
    fs::path target = out_dir / fs::path(std::u8string(paths[i].begin(), paths[i].end()));
    std::error_code ec;
    if (e.dir) {
      fs::create_directories(target, ec);
      if (ec) return Fail(error, "cannot create " + Utf8(target));
      continue;
    }
    fs::create_directories(target.parent_path(), ec);
    if (ec) return Fail(error, "cannot create " + Utf8(target.parent_path()));
    if (progress && !progress(done, total, paths[i])) return ExtractResult::kCancelled;

    fs::path part = target;
    part += ".part";
    // Closes and deletes the partial file, then reports.
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out) return Fail(error, "cannot write " + Utf8(part));
    auto abandon = [&](ExtractResult r, const std::string& msg) {
      out.close();
      std::error_code rm;
      fs::remove(part, rm);
      if (r == ExtractResult::kFailed) Fail(error, msg);
      return r;
    };

    uint64_t remaining = e.size;
    uint32_t block = e.start;
    uint32_t left = e.blocks;
    while (left > 0) {
      size_t n = 0;
      for (; n < kChunkBlocks && left > 0; ++n, --left) {
        size_t want = static_cast<size_t>(std::min<uint64_t>(kBlock, remaining));
        if (want > 0 && !stfs.ReadAt(stfs.BlockOffset(block), buf.data() + n * kBlock, want)) {
          return abandon(ExtractResult::kFailed, "package is truncated at " + paths[i]);
        }
        out.write(reinterpret_cast<const char*>(buf.data() + n * kBlock),
                  static_cast<std::streamsize>(want));
        remaining -= want;
        if (left > 1 && !stfs.NextBlock(block, &block)) {
          return abandon(ExtractResult::kFailed, "package is truncated at " + paths[i]);
        }
      }
      if (left > 0 && progress &&
          !progress(done + (e.size - remaining), total, paths[i])) {
        return abandon(ExtractResult::kCancelled, "");
      }
    }
    out.close();
    if (!out || remaining != 0) return abandon(ExtractResult::kFailed, "short or failed write: " + paths[i]);
    fs::rename(part, target, ec);
    if (ec) return abandon(ExtractResult::kFailed, "cannot rename " + Utf8(part));
    done += e.size;
  }
  if (progress) progress(total, total, "");
  return ExtractResult::kOk;
}

}  // namespace gow3::setup

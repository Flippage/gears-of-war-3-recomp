#define _CRT_SECURE_NO_WARNINGS
#include "test_util.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

static fs::path ExeDir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4];
  DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
  if (n == 0 || n >= std::size(buf)) {
    // TempDir deletes folders; never guess a path.
    std::fprintf(stderr, "test-util: cannot determine test exe path\n");
    std::exit(2);
  }
  return fs::path(std::wstring(buf, n)).parent_path();
#else
  return fs::read_symlink("/proc/self/exe").parent_path();
#endif
}

fs::path TempDir(const std::string& name) {
  fs::path dir = ExeDir() / "test-tmp" / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  if (ec) std::fprintf(stderr, "test-util: remove_all(%s) failed: %s\n", dir.string().c_str(), ec.message().c_str());
  ec.clear();
  fs::create_directories(dir, ec);
  if (ec) std::fprintf(stderr, "test-util: create_directories(%s) failed: %s\n", dir.string().c_str(), ec.message().c_str());
  return dir;
}

std::optional<fs::path> EnvPath(const char* name) {
#ifdef _WIN32
  std::wstring wname(name, name + std::strlen(name));
  const wchar_t* v = _wgetenv(wname.c_str());
  if (!v || !*v) return std::nullopt;
  return fs::path(v);
#else
  const char* v = std::getenv(name);
  if (!v || !*v) return std::nullopt;
  return fs::path(v);
#endif
}

static void PutBe32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[off + i] = static_cast<uint8_t>(v >> (24 - 8 * i));
}

std::vector<uint8_t> MakeXex(uint32_t media_id, uint32_t title_id) {
  std::vector<uint8_t> b(0x200, 0);
  b[0] = 'X'; b[1] = 'E'; b[2] = 'X'; b[3] = '2';
  PutBe32(b, 0x14, 1);
  PutBe32(b, 0x18, 0x00040006);
  PutBe32(b, 0x1C, 0x100);
  PutBe32(b, 0x100, media_id);
  PutBe32(b, 0x10C, title_id);
  return b;
}

bool WriteFile(const fs::path& path, std::string_view data) {
  if (path.has_parent_path()) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(out);
}

bool WriteFile(const fs::path& path, const std::vector<uint8_t>& data) {
  return WriteFile(path, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

std::string FileText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> FileBytes(const fs::path& path) {
  const std::string s = FileText(path);
  return std::vector<uint8_t>(s.begin(), s.end());
}

std::vector<uint8_t> MakeStfsHeader(std::string_view magic, uint32_t content_type, uint32_t title_id,
                                    uint32_t media_id, std::u16string_view display_name) {
  std::vector<uint8_t> b(0x1000, 0);
  for (size_t i = 0; i < 4 && i < magic.size(); ++i) b[i] = static_cast<uint8_t>(magic[i]);
  PutBe32(b, 0x344, content_type);
  PutBe32(b, 0x354, media_id);
  PutBe32(b, 0x360, title_id);
  for (size_t i = 0; i < display_name.size() && i < 0x40; ++i) {
    b[0x411 + i * 2] = static_cast<uint8_t>(display_name[i] >> 8);
    b[0x411 + i * 2 + 1] = static_cast<uint8_t>(display_name[i] & 0xFF);
  }
  return b;
}

static std::map<std::string, fs::path> ListFiles(const fs::path& root) {
  std::map<std::string, fs::path> out;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->is_regular_file(ec)) out[fs::relative(it->path(), root, ec).generic_string()] = it->path();
  }
  return out;
}

bool SameTree(const fs::path& a, const fs::path& b) {
  auto fa = ListFiles(a), fb = ListFiles(b);
  if (fa.size() != fb.size()) return false;
  for (auto& [rel, pa] : fa) {
    auto it = fb.find(rel);
    if (it == fb.end()) return false;
    std::error_code ec;
    if (fs::file_size(pa, ec) != fs::file_size(it->second, ec)) return false;
    std::ifstream x(pa, std::ios::binary), y(it->second, std::ios::binary);
    std::vector<char> bx(1 << 16), by(1 << 16);
    while (x && y) {
      x.read(bx.data(), bx.size());
      y.read(by.data(), by.size());
      if (x.gcount() != y.gcount() || std::memcmp(bx.data(), by.data(), static_cast<size_t>(x.gcount())) != 0)
        return false;
    }
  }
  return true;
}

bool AnyFileEndsWith(const fs::path& dir, const std::string& suffix) {
  for (auto& [rel, p] : ListFiles(dir)) {
    if (rel.size() >= suffix.size() && rel.compare(rel.size() - suffix.size(), suffix.size(), suffix) == 0)
      return true;
  }
  return false;
}

std::vector<uint8_t> MakeStfsPackage(const std::vector<std::pair<std::string, std::string>>& files) {
  // Layout (block_sep = 1, one table per level): header 0x1000, hash table 0x1000, then blocks
  // 0.. at +0x1000 each. Block 0 is the file table; files follow.
  auto put24le = [](std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off] = v & 0xFF; b[off + 1] = (v >> 8) & 0xFF; b[off + 2] = (v >> 16) & 0xFF;
  };
  std::vector<uint8_t> h = MakeStfsHeader("LIVE", 0, 0, 0, u"");
  PutBe32(h, 0x340, 0x1000);
  h[0x379] = 0x24;
  h[0x379 + 2] = 1;  // block separation: read only
  h[0x379 + 3] = 1;  // file table block count (LE16)
  put24le(h, 0x379 + 5, 0);

  std::vector<uint8_t> table(0x1000, 0), hashes(0x1000, 0), data;
  auto set_next = [&](uint32_t block, uint32_t next) {
    size_t at = block * 0x18 + 0x15;
    hashes[at] = next >> 16; hashes[at + 1] = (next >> 8) & 0xFF; hashes[at + 2] = next & 0xFF;
  };
  set_next(0, 0xFFFFFF);
  uint32_t next_block = 1;
  for (size_t i = 0; i < files.size(); ++i) {
    const auto& [name, content] = files[i];
    uint8_t* e = &table[i * 0x40];
    std::memcpy(e, name.data(), name.size());
    e[0x28] = static_cast<uint8_t>(name.size() | 0x40);
    uint32_t blocks = static_cast<uint32_t>((content.size() + 0xFFF) / 0x1000);
    put24le(table, i * 0x40 + 0x29, blocks);
    put24le(table, i * 0x40 + 0x2F, next_block);
    e[0x32] = 0xFF; e[0x33] = 0xFF;  // parent: root
    PutBe32(table, i * 0x40 + 0x34, static_cast<uint32_t>(content.size()));
    for (uint32_t k = 0; k < blocks; ++k) set_next(next_block + k, k + 1 < blocks ? next_block + k + 1 : 0xFFFFFF);
    size_t at = data.size();
    data.resize(at + size_t{blocks} * 0x1000, 0);
    std::memcpy(&data[at], content.data(), content.size());
    next_block += blocks;
  }
  h.insert(h.end(), hashes.begin(), hashes.end());
  h.insert(h.end(), table.begin(), table.end());
  h.insert(h.end(), data.begin(), data.end());
  return h;
}

std::vector<uint8_t> Bytes(size_t n) {
  std::vector<uint8_t> b(n);
  for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(i * 31 + 7);
  return b;
}

namespace {
struct XNode {
  std::map<std::string, XNode> dirs;
  std::map<std::string, std::vector<uint8_t>> files;
  uint32_t sector = 0, size = 0;
  std::map<std::string, uint32_t> file_sectors;
};
size_t EntryLen(const std::string& name) { return (14 + name.size() + 3) & ~size_t{3}; }
// Entries never straddle a sector: the rest of the sector stays 0xFF and the next entry starts
// on the next sector, as on real discs.
size_t XPlace(size_t off, size_t len) { return (off % 2048 + len > 2048) ? (off + 2047) & ~size_t{2047} : off; }
std::vector<std::string> SortedNames(const XNode& n) {
  std::vector<std::string> names;
  for (auto& [k, v] : n.dirs) names.push_back(k);
  for (auto& [k, v] : n.files) names.push_back(k);
  std::sort(names.begin(), names.end());
  return names;
}
bool XEmpty(const XNode& n) { return n.dirs.empty() && n.files.empty(); }
void XLayout(XNode& n, uint32_t& next, std::vector<XNode*>& order, bool is_root) {
  if (XEmpty(n) && !is_root) return;  // empty directories are sector 0, size 0
  size_t off = 0;
  for (const std::string& name : SortedNames(n)) off = XPlace(off, EntryLen(name)) + EntryLen(name);
  n.size = static_cast<uint32_t>(std::max<size_t>(2048, (off + 2047) & ~size_t{2047}));
  n.sector = next;
  next += n.size / 2048;
  order.push_back(&n);
  for (auto& [k, v] : n.dirs) XLayout(v, next, order, false);
}
}  // namespace

std::vector<uint8_t> MakeXiso(uint64_t partition_offset,
                              std::vector<std::pair<std::string, std::vector<uint8_t>>> files) {
  XNode root;
  for (auto& [path, data] : files) {
    XNode* n = &root;
    size_t pos;
    while ((pos = path.find('\\')) != std::string::npos) {
      n = &n->dirs[path.substr(0, pos)];
      path.erase(0, pos + 1);
    }
    if (!path.empty()) n->files[path] = std::move(data);  // a trailing '\' only makes the directory
  }
  uint32_t next = 33;  // sector 32 holds the volume descriptor
  std::vector<XNode*> order;
  XLayout(root, next, order, true);
  for (XNode* n : order) {
    for (auto& [k, v] : n->files) {
      n->file_sectors[k] = next;
      next += static_cast<uint32_t>((v.size() + 2047) / 2048);
    }
  }
  std::vector<uint8_t> img(partition_offset + uint64_t{next} * 2048, 0);
  auto put32 = [&](size_t off, uint32_t v) {
    for (int i = 0; i < 4; ++i) img[off + i] = static_cast<uint8_t>(v >> (8 * i));
  };
  auto put16 = [&](size_t off, uint32_t v) {
    img[off] = static_cast<uint8_t>(v);
    img[off + 1] = static_cast<uint8_t>(v >> 8);
  };
  size_t vd = partition_offset + 0x10000;
  std::memcpy(&img[vd], "MICROSOFT*XBOX*MEDIA", 20);
  put32(vd + 20, root.sector);
  put32(vd + 24, root.size);

  for (XNode* n : order) {
    // Degenerate tree: every entry's right link points at the next one.
    struct Item { const std::string* name; uint32_t sector, size; uint8_t attrib; };
    std::vector<Item> items;
    for (auto& [k, v] : n->dirs) items.push_back({&k, v.sector, v.size, 0x10});
    for (auto& [k, v] : n->files)
      items.push_back({&k, n->file_sectors[k], static_cast<uint32_t>(v.size()), 0x20});
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return *a.name < *b.name; });
    size_t base = partition_offset + uint64_t{n->sector} * 2048, off = 0;
    std::fill_n(img.begin() + static_cast<std::ptrdiff_t>(base), n->size, uint8_t{0xFF});
    for (size_t i = 0; i < items.size(); ++i) {
      size_t len = EntryLen(*items[i].name);
      off = XPlace(off, len);
      size_t next_off = i + 1 < items.size() ? XPlace(off + len, EntryLen(*items[i + 1].name)) : 0;
      put16(base + off, 0);
      put16(base + off + 2, static_cast<uint32_t>(next_off / 4));
      put32(base + off + 4, items[i].sector);
      put32(base + off + 8, items[i].size);
      img[base + off + 12] = items[i].attrib;
      img[base + off + 13] = static_cast<uint8_t>(items[i].name->size());
      std::memcpy(&img[base + off + 14], items[i].name->data(), items[i].name->size());
      off += len;
    }
    for (auto& [k, v] : n->files) {
      if (!v.empty()) std::memcpy(&img[partition_offset + uint64_t{n->file_sectors[k]} * 2048], v.data(), v.size());
    }
  }
  return img;
}

void MakeFakeGame(const fs::path& dir) {
  WriteFile(dir / "default.xex", MakeXex(0x58122AD6, 0x4D5308AB));
  std::error_code ec;
  fs::create_directories(dir / "GearGame", ec);
}

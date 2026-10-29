#include "setup/xdvdfs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <set>
#include <span>
#include <system_error>

#include "setup/be.h"
#include "setup/path_util.h"

namespace gow3::setup {

namespace fs = std::filesystem;

namespace {
constexpr uint64_t kSector = 2048;
constexpr uint64_t kPartitions[] = {0, 0x2080000, 0xFD90000, 0x18300000};
constexpr uint64_t kDescriptorOffset = 0x10000;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr size_t kMagicLen = 20;
constexpr uint32_t kMaxDirBytes = 64u << 20;
constexpr size_t kMaxEntries = 1u << 20;
constexpr size_t kChunk = 4u << 20;

bool ReadAt(std::ifstream& in, uint64_t off, void* dst, size_t n) {
  in.clear();
  in.seekg(static_cast<std::streamoff>(off));
  in.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
  return static_cast<bool>(in) && static_cast<size_t>(in.gcount()) == n;
}

// Names are single-byte (latin-1); store as UTF-8.
std::string NameToUtf8(std::span<const uint8_t> raw) {
  std::string out;
  for (uint8_t c : raw) {
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back(static_cast<char>(0xC0 | (c >> 6)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

std::optional<DiscImage> Bad(std::string* error, const std::string& msg) {
  if (error) *error = msg;
  return std::nullopt;
}

ExtractResult Fail(std::string* error, const std::string& msg) {
  if (error) *error = msg;
  return ExtractResult::kFailed;
}

bool EqualNoCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

struct DirJob {
  uint32_t sector, size;
  std::string prefix;  // "" or "dir\"
};
}  // namespace

std::optional<DiscImage> DiscImage::Open(const fs::path& iso, std::string* error) {
  std::ifstream in(iso, std::ios::binary);
  if (!in) return Bad(error, "cannot open " + Utf8(iso));
  in.seekg(0, std::ios::end);
  const uint64_t image_size = static_cast<uint64_t>(in.tellg());

  uint64_t partition = 0;
  uint32_t root_sector = 0, root_size = 0;
  bool found = false;
  for (uint64_t p : kPartitions) {
    std::array<uint8_t, 28> d{};
    if (!ReadAt(in, p + kDescriptorOffset, d.data(), d.size())) continue;
    if (std::memcmp(d.data(), kMagic, kMagicLen) != 0) continue;
    partition = p;
    root_sector = Le32(d, 20);
    root_size = Le32(d, 24);
    found = true;
    break;
  }
  if (!found) return Bad(error, "not an Xbox 360 disc image: " + Utf8(iso));

  DiscImage img;
  img.path_ = iso;
  std::vector<DirJob> jobs;
  std::set<uint32_t> seen_dirs;
  jobs.push_back({root_sector, root_size, ""});
  seen_dirs.insert(root_sector);
  while (!jobs.empty()) {
    DirJob job = std::move(jobs.back());
    jobs.pop_back();
    if (job.size == 0) continue;  // empty directory
    if (job.size > kMaxDirBytes) return Bad(error, "corrupt disc: directory too large");
    std::vector<uint8_t> dir(job.size);
    if (!ReadAt(in, partition + uint64_t{job.sector} * kSector, dir.data(), dir.size()))
      return Bad(error, "corrupt disc: directory beyond end of image");

    std::set<uint32_t> seen;  // entry offsets already visited in this directory
    std::vector<uint32_t> todo{0};
    while (!todo.empty()) {
      uint32_t off = todo.back();
      todo.pop_back();
      if (uint64_t{off} + 14 > dir.size() || !seen.insert(off).second) continue;
      uint16_t left = Le16(dir, off), right = Le16(dir, off + 2);
      if (left == 0xFFFF && right == 0xFFFF) continue;  // padding
      uint32_t sector = Le32(dir, off + 4), size = Le32(dir, off + 8);
      uint8_t attrib = dir[off + 12], name_len = dir[off + 13];
      if (uint64_t{off} + 14 + name_len > dir.size())
        return Bad(error, "corrupt disc: directory entry overruns");
      if (left) todo.push_back(uint32_t{left} * 4);
      if (right) todo.push_back(uint32_t{right} * 4);

      std::string name = NameToUtf8(std::span<const uint8_t>(dir).subspan(off + 14, name_len));
      if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        return Bad(error, "unsafe name on disc: " + name);
      std::string path = job.prefix + name;
      if (!SafeName(path)) return Bad(error, "unsafe path on disc: " + path);
      if (img.entries_.size() >= kMaxEntries) return Bad(error, "corrupt disc: too many entries");

      bool is_dir = (attrib & 0x10) != 0;
      uint64_t abs = partition + uint64_t{sector} * kSector;
      if (is_dir) {
        if (!seen_dirs.insert(sector).second) return Bad(error, "corrupt disc: directory loop at " + path);
        jobs.push_back({sector, size, path + "\\"});
        img.entries_.push_back({path, abs, 0, true});
      } else {
        if (size > 0 && abs + size > image_size) return Bad(error, "image is truncated at " + path);
        img.entries_.push_back({path, abs, size, false});
        img.total_ += size;
      }
    }
  }
  std::sort(img.entries_.begin(), img.entries_.end(),
            [](const DiscEntry& a, const DiscEntry& b) { return a.path < b.path; });
  return img;
}

std::optional<std::vector<uint8_t>> DiscImage::ReadFile(std::string_view path, size_t max_bytes) const {
  for (const DiscEntry& e : entries_) {
    if (e.dir || !EqualNoCase(e.path, path)) continue;
    std::ifstream in(path_, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<uint8_t> buf(static_cast<size_t>(std::min<uint64_t>(e.size, max_bytes)));
    if (!buf.empty() && !ReadAt(in, e.offset, buf.data(), buf.size())) return std::nullopt;
    return buf;
  }
  return std::nullopt;
}

ExtractResult DiscImage::ExtractAll(const fs::path& out_dir, const Progress& progress,
                                    std::string* error) const {
  std::ifstream in(path_, std::ios::binary);
  if (!in) return Fail(error, "cannot open " + Utf8(path_));
  uint64_t done = 0;
  std::vector<uint8_t> buf(kChunk);
  for (const DiscEntry& e : entries_) {
    fs::path target = out_dir / fs::path(std::u8string(e.path.begin(), e.path.end()));
    std::error_code ec;
    if (e.dir) {
      fs::create_directories(target, ec);
      if (ec) return Fail(error, "cannot create " + Utf8(target));
      continue;
    }
    fs::create_directories(target.parent_path(), ec);
    if (ec) return Fail(error, "cannot create " + Utf8(target.parent_path()));
    if (progress && !progress(done, total_, e.path)) return ExtractResult::kCancelled;

    fs::path part = target;
    part += ".part";
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out) return Fail(error, "cannot write " + Utf8(part));
    auto abandon = [&](ExtractResult r, const std::string& msg) {
      out.close();
      std::error_code rm;
      fs::remove(part, rm);
      return r == ExtractResult::kFailed ? Fail(error, msg) : r;
    };

    uint64_t copied = 0;
    while (copied < e.size) {
      size_t n = static_cast<size_t>(std::min<uint64_t>(kChunk, e.size - copied));
      if (!ReadAt(in, e.offset + copied, buf.data(), n))
        return abandon(ExtractResult::kFailed, "image is truncated at " + e.path);
      out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(n));
      if (!out) return abandon(ExtractResult::kFailed, "write failed: " + e.path);
      copied += n;
      if (progress && !progress(done + copied, total_, e.path))
        return abandon(ExtractResult::kCancelled, "");
    }
    out.close();
    if (!out) return abandon(ExtractResult::kFailed, "write failed: " + e.path);
    fs::rename(part, target, ec);  // replaces an existing file
    if (ec) return abandon(ExtractResult::kFailed, "cannot rename " + Utf8(part));
    done += e.size;
  }
  if (progress) progress(total_, total_, "");
  return ExtractResult::kOk;
}

}  // namespace gow3::setup

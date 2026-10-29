#include "setup/finder.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "setup/state.h"
#include "setup/xdvdfs.h"

namespace gow3::setup {
namespace fs = std::filesystem;

namespace {

std::string LowerExt(const fs::path& p) {
  std::string e = p.extension().string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
  return e;
}

// Each root, then its direct subfolders (de-duplicated).
std::vector<fs::path> WithSubfolders(const std::vector<fs::path>& roots) {
  std::vector<fs::path> dirs;
  std::set<fs::path> seen;
  auto add = [&](const fs::path& d) {
    std::error_code ec;
    fs::path key = fs::weakly_canonical(d, ec);
    if (ec) key = d;
    if (seen.insert(key).second) dirs.push_back(d);
  };
  for (const auto& root : roots) {
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) continue;
    add(root);
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
      std::error_code e2;
      if (it->is_directory(e2)) add(it->path());
    }
  }
  return dirs;
}

// Visits regular files directly inside each folder.
template <typename F>
void ForEachFile(const std::vector<fs::path>& dirs, F&& fn) {
  for (const auto& dir : dirs) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
      std::error_code e2;
      if (!it->is_regular_file(e2)) continue;
      const uint64_t size = it->file_size(e2);
      if (e2) continue;
      fn(it->path(), size);
    }
  }
}

}  // namespace

bool IsGameFolder(const fs::path& dir) {
  std::error_code ec;
  return !dir.empty() && fs::exists(dir / "default.xex", ec) && fs::is_directory(dir / "GearGame", ec);
}

std::vector<fs::path> SearchRoots(const fs::path& start, int parents) {
  std::vector<fs::path> roots;
  fs::path dir = start;
  for (int i = 0; i <= parents && !dir.empty(); ++i) {
    roots.push_back(dir);
    if (dir == dir.parent_path()) break;
    dir = dir.parent_path();
  }
  return roots;
}

FindResult FindGame(const std::vector<fs::path>& roots) {
  FindResult result;
  const auto dirs = WithSubfolders(roots);
  for (const auto& d : dirs) {
    if (IsGameFolder(d)) {
      result.game = d;
      break;
    }
  }
  ForEachFile(dirs, [&](const fs::path& file, uint64_t size) {
    if (LowerExt(file) != ".iso") return;
    auto disc = DiscImage::Open(file, nullptr);
    if (!disc) return;
    auto bytes = disc->ReadFile("default.xex", 0x10000);
    if (!bytes) return;
    auto xex = ReadXexInfo(*bytes);
    if (xex && xex->title_id == kGow3TitleId) result.isos.push_back({file, size, *xex});
  });
  return result;
}

std::vector<FoundPackage> FindPackages(const std::vector<fs::path>& roots, uint32_t disc_media_id) {
  static const std::set<std::string> kSkip = {".iso", ".zip", ".7z", ".rar", ".exe",
                                              ".dll", ".txt", ".xex", ".xexp"};
  std::vector<FoundPackage> found;
  std::set<fs::path> seen;
  ForEachFile(WithSubfolders(roots), [&](const fs::path& file, uint64_t size) {
    if (size < 0x1000 || kSkip.count(LowerExt(file))) return;
    std::error_code ec;
    fs::path key = fs::weakly_canonical(file, ec);
    if (ec) key = file;
    if (!seen.insert(key).second) return;
    auto h = ReadStfsHeader(file);
    if (!h || h->title_id != kGow3TitleId) return;
    if (h->content_type != kTuType && h->content_type != kDlcType) return;
    const bool tu = h->content_type == kTuType;
    found.push_back({file, size, PackageId(file, size), *h, tu, !tu || h->media_id == disc_media_id});
  });
  return found;
}

}  // namespace gow3::setup

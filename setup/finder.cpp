#include "setup/finder.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <set>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "setup/state.h"
#include "setup/xdvdfs.h"

namespace gow3::setup {
namespace fs = std::filesystem;

namespace {

std::wstring Lower(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return s;
}

std::wstring LowerExt(const fs::path& p) { return Lower(p.extension().native()); }

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
  return !dir.empty() && fs::exists(dir / "default.xex", ec) && fs::is_directory(dir / "GearGame", ec) &&
         !fs::exists(dir / kExtractingMarker, ec);
}

fs::path FindGameFolderIn(const fs::path& dir) {
  if (IsGameFolder(dir)) return dir;
  std::error_code ec;
  for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec)) {
    if (IsGameFolder(it->path())) return it->path();
  }
  return {};
}

fs::path ResolveGameFolder(const fs::path& given, const fs::path& exe_dir) {
  if (IsGameFolder(given)) return given;
  if (const fs::path path = ReadGamePathFile(exe_dir / "gow3_game_path.txt"); !path.empty()) {
    if (fs::path hit = FindGameFolderIn(path); !hit.empty()) return hit;
  }
  fs::path dir = exe_dir;
  for (int depth = 0; depth <= 4 && !dir.empty(); ++depth, dir = dir.parent_path()) {
    if (fs::path hit = FindGameFolderIn(dir); !hit.empty()) return hit;
    if (dir == dir.parent_path()) break;
  }
  return given;
}

fs::path GameDataRootArg(const std::wstring& args) {
  // CommandLineToArgvW reads the first token as the program name, by other rules: give it one.
  const std::wstring line = L"gow3.exe " + args;
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(line.c_str(), &argc);
  if (!argv) return {};
  constexpr std::wstring_view kFlag = L"--game_data_root";
  fs::path out;
  for (int i = 1; i < argc; ++i) {
    const std::wstring_view a = argv[i];
    if (a.starts_with(kFlag) && a.size() > kFlag.size() && a[kFlag.size()] == L'=') {
      out = fs::path(a.substr(kFlag.size() + 1));
    } else if (a == kFlag && i + 1 < argc) {
      out = fs::path(argv[++i]);
    }
  }
  LocalFree(argv);
  return out;
}

std::vector<fs::path> TitleUpdateCandidates(const fs::path& game, const fs::path& exe_dir, const fs::path& configured) {
  if (!configured.empty()) return {configured};
  return {exe_dir / "TU6", game / "TU6"};
}

std::optional<fs::path> FindTitleUpdateFolderIn(const fs::path& game, const fs::path& exe_dir,
                                                const fs::path& configured) {
  std::error_code ec;
  for (const fs::path& folder : TitleUpdateCandidates(game, exe_dir, configured)) {
    if (fs::is_regular_file(folder / "default.xexp", ec)) return folder;
  }
  return std::nullopt;
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

std::vector<fs::path> PackageRoots(const fs::path& exe_dir, const fs::path& game) {
  std::vector<fs::path> roots = SearchRoots(exe_dir, 2);
  if (!game.empty()) {
    roots.push_back(game);
    if (game.has_parent_path()) roots.push_back(game.parent_path());
  }
  return roots;
}

FindResult FindGame(const std::vector<fs::path>& roots) {
  FindResult result;
  const auto dirs = WithSubfolders(roots);
  for (const auto& d : dirs) {
    if (IsGameFolder(d)) {
      result.game = d;
      return result;
    }
  }
  ForEachFile(dirs, [&](const fs::path& file, uint64_t size) {
    if (LowerExt(file) != L".iso") return;
    auto disc = DiscImage::Open(file, nullptr);
    if (!disc) return;
    auto bytes = disc->ReadFile("default.xex", 0x10000);
    if (!bytes) return;
    auto xex = ReadXexInfo(*bytes);
    if (xex && xex->title_id == kGow3TitleId) result.isos.push_back({file, size, *xex});
  });
  return result;
}

bool IsDiscOwnPackage(const fs::path& file) {
  return Lower(file.filename().native()) == L"avatarawards";
}

namespace {

// Windows file attributes that make reading a file slow or recursive: cloud-only files (OneDrive and
// similar download them on first read) and reparse points (junctions, links back up the tree).
constexpr DWORD kCloudOnly = FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_OPEN |
                             FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS;

DWORD Attributes(const fs::path& p) {
  const DWORD a = GetFileAttributesW(p.c_str());
  return a == INVALID_FILE_ATTRIBUTES ? 0 : a;
}

struct PackageScan {
  uint32_t disc_media_id;
  std::chrono::steady_clock::time_point deadline = (std::chrono::steady_clock::time_point::max)();
  std::vector<FoundPackage> found;
  std::set<fs::path> seen;
  int entries = 0;
  bool stopped = false;  // the entry limit or the deadline was reached

  bool Stop() {
    if (!stopped && (++entries > kPackageScanLimit || std::chrono::steady_clock::now() > deadline)) stopped = true;
    return stopped;
  }

  void File(const fs::path& file, uint64_t size) {
    static const std::set<std::wstring> kSkip = {L".iso", L".zip", L".7z", L".rar", L".exe",
                                                 L".dll", L".txt", L".xex", L".xexp"};
    if (size < 0x1000 || kSkip.count(LowerExt(file)) || IsDiscOwnPackage(file)) return;
    if (Attributes(file) & kCloudOnly) return;  // never trigger a download just to read a header
    std::error_code ec;
    fs::path key = fs::weakly_canonical(file, ec);
    if (ec) key = file;
    if (!seen.insert(key).second) return;
    auto h = ReadStfsHeader(file);
    if (!h || h->title_id != kGow3TitleId) return;
    if (h->content_type != kTuType && h->content_type != kDlcType) return;
    const bool tu = h->content_type == kTuType;
    found.push_back({file, size, PackageId(file, size), *h, tu, !tu || h->media_id == disc_media_id});
  }

  void Folder(const fs::path& dir, int depth) {
    static const std::set<std::wstring> kSkipDirs = {L"geargame",L"$systemupdate", L".git", L"out",
                                                     L"generated", L"node_modules", L"test-tmp", L"tu6.part-dir",
                                                     L"tu6.old"};
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
      if (Stop()) return;
      std::error_code e2;
      if (it->is_directory(e2)) {
        if (depth > 0 && !it->is_symlink(e2) && !(Attributes(it->path()) & (FILE_ATTRIBUTE_REPARSE_POINT | kCloudOnly)) &&
            !kSkipDirs.count(Lower(it->path().filename().native()))) {
          Folder(it->path(), depth - 1);
        }
      } else if (it->is_regular_file(e2)) {
        const uint64_t size = it->file_size(e2);
        if (!e2) File(it->path(), size);
      }
    }
  }
};

}  // namespace

std::vector<FoundPackage> FindPackages(const std::vector<fs::path>& roots, uint32_t disc_media_id,
                                       std::chrono::milliseconds budget) {
  PackageScan scan{disc_media_id};
  if (budget.count() > 0) scan.deadline = std::chrono::steady_clock::now() + budget;
  std::set<fs::path> done;
  for (const fs::path& root : roots) {
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) continue;
    fs::path key = fs::weakly_canonical(root, ec);
    if (ec) key = root;
    if (!done.insert(key).second) continue;
    scan.Folder(root, kPackageDepth);
    if (scan.stopped) break;
  }
  return std::move(scan.found);
}

std::vector<FoundPackage> FindPackagesAt(const fs::path& file_or_folder, uint32_t disc_media_id) {
  std::error_code ec;
  if (fs::is_directory(file_or_folder, ec)) return FindPackages({file_or_folder}, disc_media_id);
  PackageScan scan{disc_media_id};
  const uint64_t size = fs::file_size(file_or_folder, ec);
  if (!ec) scan.File(file_or_folder, size);
  return std::move(scan.found);
}

}  // namespace gow3::setup

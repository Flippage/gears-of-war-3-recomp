#include "setup/update_files.h"

#include <algorithm>
#include <iterator>
#include <system_error>

#include "setup/path_util.h"

namespace fs = std::filesystem;

namespace gow3::setup {
namespace {

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; });
  return s;
}

bool Space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

}  // namespace

std::optional<std::string> ParseSha256File(std::string_view text) {
  size_t b = 0;
  while (b < text.size() && Space(text[b])) ++b;
  size_t e = b;
  while (e < text.size() && !Space(text[e])) ++e;
  const std::string hash = Lower(std::string(text.substr(b, e - b)));
  if (hash.size() != 64) return std::nullopt;
  for (char c : hash) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return std::nullopt;
  }
  return hash;
}

std::optional<std::string> UnsafeEntry(std::string_view listing) {
  while (!listing.empty()) {
    const size_t nl = listing.find('\n');
    std::string_view line = listing.substr(0, nl);
    listing.remove_prefix(nl == std::string_view::npos ? listing.size() : nl + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (!line.empty() && !SafeName(std::string(line))) return std::string(line);
  }
  return std::nullopt;
}

std::vector<fs::path> FilesToCopy(const fs::path& src) {
  std::vector<fs::path> files;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(src, ec), end; !ec && it != end; it.increment(ec)) {
    if (!fs::is_regular_file(it->symlink_status(ec))) continue;
    const fs::path rel = it->path().lexically_relative(src);
    if (!SafeName(Utf8(rel))) continue;
    const std::string first = Lower(Utf8(*rel.begin()));
    if (first == "update.tmp") continue;
    if (std::next(rel.begin()) == rel.end() &&
        (first == "gow3.toml" || first == "gow3_game_path.txt" || first == "launcher.ini" || first == "gow3.exe" ||
         first == "launcher.old.exe" || first == "version.txt")) {
      continue;
    }
    files.push_back(rel);
  }
  return files;
}

bool UpdateBackupPending(const fs::path& release_dir) {
  std::error_code ec;
  return fs::exists(release_dir / "update.tmp" / kRollbackFailedMarker, ec);
}

void RemoveStaleUpdateTmp(const fs::path& release_dir) {
  if (UpdateBackupPending(release_dir)) return;
  std::error_code ec;
  fs::remove_all(release_dir / "update.tmp", ec);
}

}  // namespace gow3::setup

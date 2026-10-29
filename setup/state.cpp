#include "setup/state.h"

#include <charconv>
#include <fstream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "setup/path_util.h"

namespace fs = std::filesystem;

namespace gow3::setup {
namespace {

std::string Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return std::string(s);
}

// Package ids are "<file name>:<size>"; a file name may hold commas, never '|'.
constexpr char kListSeparator = '|';

std::set<std::string> ParseList(std::string_view v) {
  std::set<std::string> out;
  while (!v.empty()) {
    const size_t sep = v.find(kListSeparator);
    std::string item = Trim(v.substr(0, sep));
    if (!item.empty()) out.insert(std::move(item));
    if (sep == std::string_view::npos) break;
    v.remove_prefix(sep + 1);
  }
  return out;
}

std::string JoinList(const std::set<std::string>& l) {
  std::string out;
  for (const auto& s : l) {
    if (!out.empty()) out += kListSeparator;
    out += s;
  }
  return out;
}


}  // namespace

LauncherState LoadState(const fs::path& ini) {
  LauncherState s;
  std::ifstream in(ini, std::ios::binary);
  if (!in) return s;
  s.existed = true;
  std::string line;
  while (std::getline(in, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = Trim(std::string_view(line).substr(0, eq));
    const std::string val = Trim(std::string_view(line).substr(eq + 1));
    if (key == "build") {
      s.build = val == "tu6" ? Build::kTu6 : Build::kBase;
    } else if (key == "declined") {
      s.declined = ParseList(val);
    } else if (key == "installed") {
      s.installed = ParseList(val);
    } else if (key == "skip_release") {
      s.skip_release = val;
    } else if (key == "check_updates") {
      s.check_updates = val != "false";
    } else if (key == "last_check") {
      int64_t n = 0;
      if (std::from_chars(val.data(), val.data() + val.size(), n).ec == std::errc()) s.last_check = n;
    }
  }
  return s;
}

bool SaveState(const fs::path& ini, const LauncherState& s) {
  fs::path tmp = ini;
  tmp += L".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "build = " << (s.build == Build::kTu6 ? "tu6" : "base") << "\n"
        << "declined = " << JoinList(s.declined) << "\n"
        << "installed = " << JoinList(s.installed) << "\n"
        << "skip_release = " << s.skip_release << "\n"
        << "check_updates = " << (s.check_updates ? "true" : "false") << "\n"
        << "last_check = " << s.last_check << "\n";
    out.flush();
    if (!out) return false;
  }
  if (!MoveFileExW(tmp.c_str(), ini.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::error_code ec;
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

fs::path ReadGamePathFile(const fs::path& txt) {
  std::ifstream in(txt, std::ios::binary);
  if (!in) return {};
  std::string line;
  std::getline(in, line);
  if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
  if (line.empty()) return {};
  return FromUtf8(line);
}

bool WriteGamePathFile(const fs::path& txt, const fs::path& game) {
  std::ofstream out(txt, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << Utf8(game) << "\n";
  out.flush();
  return static_cast<bool>(out);
}

std::optional<std::array<int, 3>> ParseVersion(std::string_view v) {
  if (!v.empty() && v.front() == 'v') v.remove_prefix(1);
  std::array<int, 3> r{0, 0, 0};
  int parts = 0;
  while (true) {
    const size_t dot = v.find('.');
    const std::string_view part = v.substr(0, dot);
    if (part.empty() || parts >= 3 || part.front() < '0' || part.front() > '9') return std::nullopt;
    int n = 0;
    const auto res = std::from_chars(part.data(), part.data() + part.size(), n);
    if (res.ec != std::errc() || res.ptr != part.data() + part.size()) return std::nullopt;
    r[parts++] = n;
    if (dot == std::string_view::npos) break;
    v.remove_prefix(dot + 1);
  }
  if (parts < 2) return std::nullopt;
  return r;
}

bool IsNewer(std::string_view candidate, std::string_view current) {
  const auto a = ParseVersion(candidate);
  const auto b = ParseVersion(current);
  return a && b && *a > *b;
}

std::string PackageId(const fs::path& file, uint64_t size) {
  return Utf8(file.filename()) + ":" + std::to_string(size);
}

}  // namespace gow3::setup

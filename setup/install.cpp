#include "setup/install.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <vector>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include "setup/path_util.h"

namespace fs = std::filesystem;

namespace gow3::setup {
namespace {

std::wstring Normalized(const fs::path& p) {
  std::error_code ec;
  fs::path c = fs::weakly_canonical(p, ec);
  std::wstring s = (ec ? p : c).wstring();
  std::transform(s.begin(), s.end(), s.begin(),
                 [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
  return s;
}

}  // namespace

bool IsGameRunning(const fs::path& gow3_exe) {
  std::wstring want = Normalized(gow3_exe);
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  bool found = false;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
    if (!h) continue;
    wchar_t buf[32768];
    DWORD n = static_cast<DWORD>(std::size(buf));
    if (QueryFullProcessImageNameW(h, 0, buf, &n))
      found = Normalized(fs::path(std::wstring(buf, n))) == want;
    CloseHandle(h);
  }
  CloseHandle(snap);
  return found;
}

WaitResult WaitForGameExit(const fs::path& gow3_exe, std::chrono::milliseconds wait,
                           const std::function<bool()>& cancelled) {
  const auto deadline = std::chrono::steady_clock::now() + wait;
  while (IsGameRunning(gow3_exe)) {
    if (cancelled && cancelled()) return WaitResult::kCancelled;
    if (std::chrono::steady_clock::now() >= deadline) return WaitResult::kTimedOut;
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  return WaitResult::kExited;
}

SwapResult SwapBuild(const fs::path& release_dir, Build build, std::chrono::seconds wait) {
  fs::path exe = release_dir / "gow3.exe", fresh = release_dir / "gow3.exe.new";
  fs::path src = release_dir / "builds" / (build == Build::kTu6 ? "gow3-tu6.exe" : "gow3-base.exe");
  if (WaitForGameExit(exe, wait) != WaitResult::kExited) return SwapResult::kGameRunning;
  std::error_code ec;
  if (!fs::exists(src, ec)) return SwapResult::kFailed;
  if (!CopyFileW(src.c_str(), fresh.c_str(), FALSE)) {
    fs::remove(fresh, ec);
    return SwapResult::kFailed;
  }
  // CopyFileW keeps the source's attributes; a read-only gow3.exe could not be swapped next time.
  if (const DWORD attrs = GetFileAttributesW(fresh.c_str());
      attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
    SetFileAttributesW(fresh.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
  }
  if (!MoveFileExW(fresh.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    fs::remove(fresh, ec);
    return SwapResult::kFailed;
  }
  return SwapResult::kOk;
}

bool SameFileContents(const fs::path& a, const fs::path& b) {
  std::error_code ec;
  const uintmax_t size = fs::file_size(a, ec);
  if (ec || fs::file_size(b, ec) != size || ec) return false;
  std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
  if (!fa || !fb) return false;
  std::vector<char> ba(1 << 20), bb(1 << 20);
  while (fa && fb) {
    fa.read(ba.data(), static_cast<std::streamsize>(ba.size()));
    fb.read(bb.data(), static_cast<std::streamsize>(bb.size()));
    if (fa.gcount() != fb.gcount() || !std::equal(ba.begin(), ba.begin() + fa.gcount(), bb.begin())) return false;
  }
  return fa.eof() && fb.eof();
}

ExtractResult InstallTitleUpdate(const FoundPackage& tu, const fs::path& game, const Progress& progress,
                                 std::string* error) {
  fs::path part = game / "TU6.part-dir", live = game / "TU6", old = game / "TU6.old";
  std::error_code ec;
  fs::remove_all(part, ec);
  ExtractResult r = ExtractStfs(tu.file, part, progress, error);
  if (r != ExtractResult::kOk) {
    fs::remove_all(part, ec);
    return r;
  }
  bool had_old = fs::exists(live, ec);
  if (had_old) {
    fs::remove_all(old, ec);
    fs::rename(live, old, ec);
    if (ec) {
      fs::remove_all(part, ec);
      return Fail(error, "Could not replace the existing TU6 folder");
    }
  }
  fs::rename(part, live, ec);
  if (ec) {
    std::error_code ec2, ec3;
    if (had_old) fs::rename(old, live, ec2);
    fs::remove_all(part, ec3);
    return Fail(error, ec2 ? "Could not move the title update into place, and the previous TU6 could not be "
                             "restored (it may be in TU6.old)"
                           : "Could not move the title update into place");
  }
  if (had_old) fs::remove_all(old, ec);
  return ExtractResult::kOk;
}

bool InstallDlc(const FoundPackage& dlc, const fs::path& game, std::string* error) {
  fs::path dir = game / "DLC";
  std::error_code ec;
  fs::create_directories(dir, ec);
  fs::path dst = dir / dlc.file.filename();
  if (!CopyFileW(dlc.file.c_str(), dst.c_str(), FALSE)) {
    if (error)
      *error = "Could not copy " + Utf8(dlc.file.filename()) + " (error " +
               std::to_string(GetLastError()) + ")";
    return false;
  }
  return true;
}

}  // namespace gow3::setup

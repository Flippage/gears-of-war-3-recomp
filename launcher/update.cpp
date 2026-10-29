#include "update.h"

#include <windows.h>

#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iterator>
#include <memory>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <imgui.h>

#include "app.h"
#include "pages_game.h"
#include "setup/install.h"
#include "setup/json_scan.h"
#include "setup/path_util.h"
#include "setup/state.h"
#include "setup/update_files.h"
#include "theme.h"

namespace fs = std::filesystem;
using gow3::setup::Build;
using gow3::setup::ExtractResult;
using gow3::setup::Progress;
using gow3::setup::ReleaseInfo;

namespace {

constexpr const wchar_t* kApiUrl = L"https://api.github.com/repos/Flippage/gears-of-war-3-recomp/releases/latest";
constexpr const wchar_t* kDownloadBase = L"https://github.com/Flippage/gears-of-war-3-recomp/releases/download/";
constexpr int64_t kCheckInterval = 86400;
constexpr const char* kBackupPending =
    "A previous update could not be fully undone. Your original files are in update.tmp\\backup next to the "
    "launcher: move them back into the launcher's folder, then delete update.tmp.";

std::wstring g_user_agent = L"gow3-launcher/dev";  // set by InitUpdate, before any request

std::wstring Wide(const std::string& s) {
  std::wstring w(s.size(), L'\0');
  w.resize(MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), static_cast<int>(w.size())));
  return w;
}

struct Handle {
  HINTERNET h = nullptr;
  ~Handle() {
    if (h) WinHttpCloseHandle(h);
  }
};

std::string NetError(DWORD code) {
  switch (code) {
    case ERROR_WINHTTP_TIMEOUT: return "GitHub didn't answer in time.";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR: return "No connection to GitHub.";
    case ERROR_WINHTTP_SECURE_FAILURE: return "The secure connection to GitHub failed.";
    case ERROR_WINHTTP_REDIRECT_FAILED: return "GitHub redirected to an address that isn't https.";
    default: return "The connection to GitHub failed (error " + std::to_string(code) + ").";
  }
}

// GET url (https only) and hand the body to sink(data, size, content_length) in pieces; sink
// returns false to stop. Redirects are followed, but never to plain http.
bool Fetch(const std::wstring& url, const std::wstring& user_agent, const wchar_t* accept,
           const std::function<bool(const char*, size_t, uint64_t)>& sink, std::string* error) {
  auto fail = [&](std::string msg) {
    if (error) *error = std::move(msg);
    return false;
  };
  std::wstring host(256, L'\0'), path(4096, L'\0'), extra(4096, L'\0');
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  uc.lpszHostName = host.data();
  uc.dwHostNameLength = static_cast<DWORD>(host.size());
  uc.lpszUrlPath = path.data();
  uc.dwUrlPathLength = static_cast<DWORD>(path.size());
  uc.lpszExtraInfo = extra.data();
  uc.dwExtraInfoLength = static_cast<DWORD>(extra.size());
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
    return fail("Only https downloads are allowed.");
  }
  host.resize(uc.dwHostNameLength);
  path.resize(uc.dwUrlPathLength);
  extra.resize(uc.dwExtraInfoLength);
  path += extra;

  Handle session, connect, request;
  session.h = WinHttpOpen(user_agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session.h) {  // before Windows 8.1
    session.h = WinHttpOpen(user_agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
  }
  if (!session.h) return fail(NetError(GetLastError()));
  WinHttpSetTimeouts(session.h, 30000, 30000, 30000, 30000);
  connect.h = WinHttpConnect(session.h, host.c_str(), uc.nPort, 0);
  if (!connect.h) return fail(NetError(GetLastError()));
  request.h = WinHttpOpenRequest(connect.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!request.h) return fail(NetError(GetLastError()));
  DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
  WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
  const std::wstring headers = std::wstring(L"Accept: ") + accept;
  if (!WinHttpSendRequest(request.h, headers.c_str(), static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.h, nullptr)) {
    return fail(NetError(GetLastError()));
  }
  // Where the redirects ended: still https.
  DWORD len = 0;  // bytes, asked for first: a signed download URL can be long
  WinHttpQueryOption(request.h, WINHTTP_OPTION_URL, nullptr, &len);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0) return fail(NetError(GetLastError()));
  std::wstring final_url(len / sizeof(wchar_t) + 1, L' ');
  len = static_cast<DWORD>(final_url.size() * sizeof(wchar_t));
  if (!WinHttpQueryOption(request.h, WINHTTP_OPTION_URL, final_url.data(), &len)) {
    return fail(NetError(GetLastError()));
  }
  if (std::wstring_view(final_url).substr(0, 8) != L"https://") {
    return fail("GitHub redirected to an address that isn't https.");
  }
  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &status, &size, WINHTTP_NO_HEADER_INDEX);
  if (status == 403 || status == 429) return fail("GitHub's rate limit was reached. Try again in an hour.");
  if (status == 404) return fail("GitHub has no release of this project yet.");
  if (status != 200) return fail("GitHub answered with HTTP " + std::to_string(status) + ".");
  uint64_t total = 0;
  wchar_t length[32];
  size = sizeof(length);
  if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length, &size,
                          WINHTTP_NO_HEADER_INDEX)) {
    total = _wcstoui64(length, nullptr, 10);
  }
  std::vector<char> buf(64 * 1024);
  for (;;) {
    DWORD got = 0;
    if (!WinHttpReadData(request.h, buf.data(), static_cast<DWORD>(buf.size()), &got)) {
      return fail(NetError(GetLastError()));
    }
    if (got == 0) return true;
    if (!sink(buf.data(), got, total)) return false;
  }
}

// A Windows error code as text, without the trailing period and line break.
std::string WinError(DWORD code) {
  std::string s = std::system_category().message(static_cast<int>(code));
  while (!s.empty() && (s.back() == '.' || s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
  return s.empty() ? "error " + std::to_string(code) : s;
}

constexpr int kTarStopped = -2;

// Runs System32\tar.exe in `cwd` (never a PATH lookup); stdout to `out` when given. It inherits only
// its own std handles. Waits up to 10 minutes, polling `cancelled`; on either it is terminated and
// kTarStopped returned. Otherwise the exit code, or -1 when it couldn't run.
int RunTar(const std::wstring& args, const fs::path& cwd, const fs::path& out, const std::function<bool()>& cancelled) {
  wchar_t system[MAX_PATH];
  const UINT n = GetSystemDirectoryW(system, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return -1;
  const fs::path tar = fs::path(system) / "tar.exe";
  std::wstring command = L"\"" + tar.wstring() + L"\" " + args;
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                           OPEN_EXISTING, 0, nullptr);
  HANDLE file = out.empty() ? INVALID_HANDLE_VALUE
                            : CreateFileW(out.c_str(), GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, 0, nullptr);
  int code = -1;
  if (nul != INVALID_HANDLE_VALUE && (out.empty() || file != INVALID_HANDLE_VALUE)) {
    HANDLE inherit[2] = {nul, file};
    const DWORD inherit_count = file != INVALID_HANDLE_VALUE ? 2 : 1;
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<char> attr_buf(size);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
    if (InitializeProcThreadAttributeList(attrs, 1, 0, &size)) {
      STARTUPINFOEXW si{};
      si.StartupInfo.cb = sizeof(si);
      si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
      si.StartupInfo.hStdInput = nul;
      si.StartupInfo.hStdOutput = file != INVALID_HANDLE_VALUE ? file : nul;
      si.StartupInfo.hStdError = nul;
      si.lpAttributeList = attrs;
      PROCESS_INFORMATION pi{};
      if (UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
                                    inherit_count * sizeof(HANDLE), nullptr, nullptr) &&
          CreateProcessW(tar.c_str(), command.data(), nullptr, nullptr, TRUE,
                         CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, cwd.c_str(), &si.StartupInfo,
                         &pi)) {
        const ULONGLONG deadline = GetTickCount64() + 10 * 60 * 1000;
        for (;;) {
          if (WaitForSingleObject(pi.hProcess, 250) == WAIT_OBJECT_0) {
            DWORD exit = 0;
            if (GetExitCodeProcess(pi.hProcess, &exit)) code = static_cast<int>(exit);
            break;
          }
          if ((cancelled && cancelled()) || GetTickCount64() > deadline) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            code = kTarStopped;
            break;
          }
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
      }
      DeleteProcThreadAttributeList(attrs);
    }
  }
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
  return code;
}

std::string ReadAll(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The body of an HTTPS GET (Fetch, at most 8 MB), with the user agent passed in: the check thread
// may outlive wWinMain, so it holds its own copy.
std::optional<std::string> GetBody(const std::wstring& url, const std::wstring& user_agent, std::string* error) {
  std::string body;
  const bool ok = Fetch(url, user_agent, L"application/vnd.github+json",
                        [&](const char* data, size_t n, uint64_t) {
                          if (body.size() + n > (8u << 20)) {  // a release answer is a few KB
                            if (error) *error = "GitHub's answer was too large.";
                            return false;
                          }
                          body.append(data, n);
                          return true;
                        },
                        error);
  if (!ok) return std::nullopt;
  return body;
}

}  // namespace

bool HttpDownload(const std::wstring& url, const fs::path& to, const std::function<bool(uint64_t, uint64_t)>& progress,
                  std::string* error) {
  std::ofstream out(to, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (error) *error = "Could not write " + gow3::setup::Utf8(to) + ".";
    return false;
  }
  uint64_t done = 0;
  const bool ok = Fetch(url, g_user_agent, L"application/octet-stream",
                        [&](const char* data, size_t n, uint64_t total) {
                          out.write(data, static_cast<std::streamsize>(n));
                          if (!out) {
                            if (error) *error = "Could not write " + gow3::setup::Utf8(to) + " (disk full?).";
                            return false;
                          }
                          done += n;
                          if (!progress(done, total)) {
                            if (error) *error = "Cancelled.";
                            return false;
                          }
                          return true;
                        },
                        error);
  out.close();
  return ok && static_cast<bool>(out);
}

std::string Sha256File(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return {};
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::string hex;
  if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
    std::vector<char> buf(1 << 20);
    bool ok = true;
    while (ok && in) {
      in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
      const std::streamsize n = in.gcount();
      if (n > 0) ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buf.data()), static_cast<ULONG>(n), 0) == 0;
    }
    unsigned char digest[32];
    if (ok && in.eof() && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
      static const char* const kHex = "0123456789abcdef";
      for (unsigned char b : digest) {
        hex += kHex[b >> 4];
        hex += kHex[b & 15];
      }
    }
    BCryptDestroyHash(hash);
  }
  BCryptCloseAlgorithmProvider(alg, 0);
  return hex;
}

UpdateResult ApplyUpdate(const fs::path& release_dir, const fs::path& zip, Build build, std::string* error,
                         const std::function<bool()>& cancelled) {
  using gow3::setup::Utf8;
  const fs::path tmp = release_dir / "update.tmp";
  auto cleanup = [&] {
    std::error_code ec;
    fs::remove_all(tmp, ec);
  };
  auto fail = [&](std::string msg) {
    if (error) *error = std::move(msg);
    cleanup();
    return UpdateResult::kFailed;
  };
  // A failed rollback's originals are in update.tmp\backup: never clean them up (or add to them).
  if (gow3::setup::UpdateBackupPending(release_dir)) {
    if (error) *error = kBackupPending;
    return UpdateResult::kFailed;
  }
  std::error_code ec;
  fs::create_directories(tmp, ec);
  if (ec) return fail("Could not create " + Utf8(tmp) + ".");

  // <hash>  <zip name>: the first field must be the zip's SHA-256.
  fs::path sha_file = zip;
  sha_file += L".sha256";
  const std::optional<std::string> want = gow3::setup::ParseSha256File(ReadAll(sha_file));
  if (!want) return fail("Could not read " + Utf8(sha_file.filename()) + ".");
  const std::string have = Sha256File(zip);
  if (have.empty()) return fail("Could not read " + Utf8(zip.filename()) + ".");
  if (*want != have) {
    if (error) *error = "The download is damaged (its SHA-256 doesn't match). Nothing was changed.";
    cleanup();
    return UpdateResult::kHashMismatch;
  }

  // tar runs inside update.tmp with relative paths where it can, so a non-ASCII release folder
  // never reaches its command line.
  const fs::path x = tmp / "x", list = tmp / "list.txt";
  fs::remove_all(x, ec);
  fs::create_directories(x, ec);
  if (ec) return fail("Could not create " + Utf8(x) + ".");
  const std::wstring zip_arg = L"\"" + (zip.parent_path() == tmp ? zip.filename() : zip).wstring() + L"\"";
  auto tar_failed = [&](int code, const char* what) {
    return fail(code == kTarStopped ? "The update was stopped (cancelled, or tar.exe ran over 10 minutes)."
                                    : std::string("The update could not be ") + what + " (tar.exe failed).");
  };
  if (const int r = RunTar(L"-tf " + zip_arg, tmp, list, cancelled); r != 0) return tar_failed(r, "read");
  if (const std::optional<std::string> bad = gow3::setup::UnsafeEntry(ReadAll(list))) {
    return fail("The update holds an unsafe path (" + *bad + "). Nothing was changed.");
  }
  if (const int r = RunTar(L"-xf " + zip_arg + L" -C x", tmp, {}, cancelled); r != 0) return tar_failed(r, "extracted");
  const fs::path src = x / zip.stem();  // gow3-recomp-<version>-win64
  for (const char* need : {"launcher.exe", "version.txt", "builds/gow3-base.exe", "builds/gow3-tu6.exe"}) {
    if (!fs::is_regular_file(src / need, ec)) {
      return fail("The update is missing " + fs::path(need).make_preferred().string() + ". Nothing was changed.");
    }
  }
  const std::vector<fs::path> files = gow3::setup::FilesToCopy(src);

  if (gow3::setup::WaitForGameExit(release_dir / "gow3.exe", std::chrono::seconds(30)) !=
      gow3::setup::WaitResult::kExited) {
    if (error) *error = "Close Gears of War 3 first, then try again.";
    cleanup();
    return UpdateResult::kGameRunning;
  }
  if (cancelled && cancelled()) return fail("The update was cancelled. Nothing was changed.");

  // From here every file that gets replaced is first moved to update.tmp\backup (gow3.exe is
  // copied there, as SwapBuild replaces it in place). A failure moves them all back, removes the
  // files that are new, and renames launcher.old.exe back to launcher.exe.
  const fs::path backup = tmp / "backup";
  struct Change {
    fs::path rel, saved;  // saved empty: the file is new
  };
  std::vector<Change> changes;
  const fs::path launcher = release_dir / "launcher.exe", old = release_dir / "launcher.old.exe";
  const bool had_launcher = fs::exists(launcher, ec);
  // The running launcher.exe can be renamed but not overwritten.
  if (had_launcher && !MoveFileExW(launcher.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    return fail("Could not rename launcher.exe (" + WinError(GetLastError()) + "). Nothing was changed.");
  }
  auto rollback = [&](std::string msg) {
    std::vector<std::string> stuck;
    for (auto it = changes.rbegin(); it != changes.rend(); ++it) {
      const fs::path to = release_dir / it->rel;
      std::error_code e;
      const bool ok = it->saved.empty() ? (fs::remove(to, e), !e)
                                        : MoveFileExW(it->saved.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
      if (!ok) stuck.push_back(Utf8(it->rel));
    }
    if (had_launcher && !MoveFileExW(old.c_str(), launcher.c_str(), MOVEFILE_REPLACE_EXISTING)) {
      stuck.push_back("launcher.exe (it is launcher.old.exe now)");
    }
    if (stuck.empty()) return fail(std::move(msg) + " Nothing was changed.");
    // Keep update.tmp: the originals that could not go back are in update.tmp\backup, and the
    // marker (naming them) stops any later start or update from removing them.
    std::string list;
    for (const std::string& s : stuck) list += (list.empty() ? "" : ", ") + s;
    std::ofstream(tmp / gow3::setup::kRollbackFailedMarker, std::ios::binary | std::ios::trunc) << list << "\n";
    if (error) {
      *error = std::move(msg) + " These files could not be put back: " + list +
               ". The originals are in update.tmp\\backup: move them back, then delete update.tmp (or unzip the release again).";
    }
    return UpdateResult::kFailed;
  };
  // Moves release/rel aside (if it exists), then copies `from` there.
  auto replace = [&](const fs::path& from, const fs::path& rel) -> std::string {
    const fs::path to = release_dir / rel, saved = backup / rel;
    std::error_code e;
    fs::create_directories(to.parent_path(), e);
    if (e) return "Could not create the folder for " + Utf8(rel) + " (" + e.message() + ").";
    if (fs::exists(to, e)) {
      fs::create_directories(saved.parent_path(), e);
      if (e || !MoveFileExW(to.c_str(), saved.c_str(), 0)) {
        return "Could not replace " + Utf8(rel) + " (" + WinError(e ? ERROR_PATH_NOT_FOUND : GetLastError()) + ").";
      }
      changes.push_back({rel, saved});
    } else {
      changes.push_back({rel, {}});
    }
    if (!CopyFileW(from.c_str(), to.c_str(), FALSE)) {
      return "Copying " + Utf8(rel) + " failed (" + WinError(GetLastError()) + ").";
    }
    return {};
  };
  for (const fs::path& rel : files) {
    if (std::string msg = replace(src / rel, rel); !msg.empty()) return rollback(std::move(msg));
  }
  {
    const fs::path gow3 = release_dir / "gow3.exe", saved = backup / "gow3.exe";
    if (fs::exists(gow3, ec)) {
      // backup\ exists only once a replaced file was moved there.
      fs::create_directories(backup, ec);
      if (!CopyFileW(gow3.c_str(), saved.c_str(), FALSE)) {
        return rollback("Could not back up gow3.exe (" + WinError(GetLastError()) + ").");
      }
      changes.push_back({"gow3.exe", saved});
    } else {
      changes.push_back({"gow3.exe", {}});
    }
  }
  const gow3::setup::SwapResult swap = gow3::setup::SwapBuild(release_dir, build, std::chrono::seconds(30));
  if (swap == gow3::setup::SwapResult::kGameRunning) return rollback("Gears of War 3 was started during the update.");
  if (swap != gow3::setup::SwapResult::kOk) {
    return rollback(std::string("Could not switch gow3.exe to builds\\gow3-") +
                    (build == Build::kTu6 ? "tu6" : "base") + ".exe.");
  }
  // Last, so an update that failed anywhere above is offered again.
  if (std::string msg = replace(src / "version.txt", "version.txt"); !msg.empty()) return rollback(std::move(msg));
  cleanup();
  return UpdateResult::kOk;
}

// ---------------------------------------------------------------------------------------------
// Check and prompt

namespace {

// The check's result, written by its thread before `done`; read by the UI thread after.
struct Check {
  std::atomic<bool> done{false};
  std::optional<ReleaseInfo> release;
  std::string error;
};

enum class Prompt { kNone, kOffer, kWorking, kFailed };

// Shared with the update job: Cancel is offered only while it downloads.
struct Outcome {
  std::atomic<bool> downloading{false};
};

struct Update {
  std::string version;  // version.txt; empty for a dev build
  std::shared_ptr<Check> check;
  std::jthread thread;
  bool manual = false;
  std::string status;  // "Check now"'s result
  Prompt prompt = Prompt::kNone;
  ReleaseInfo release;
  std::string message;  // kFailed
  std::shared_ptr<Outcome> outcome;
};
Update u;
Job g_update_job;

std::string VersionOf(const std::string& tag) { return !tag.empty() && tag[0] == 'v' ? tag.substr(1) : tag; }
std::string ZipName(const std::string& tag) { return "gow3-recomp-" + VersionOf(tag) + "-win64.zip"; }

std::string ReadVersionFile(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::string line;
  if (!in || !std::getline(in, line)) return {};
  if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
  return line;
}

void StartCheck(bool manual) {
  if (u.check) return;
  u.manual = manual;
  u.check = std::make_shared<Check>();
  u.thread = std::jthread([check = u.check, user_agent = g_user_agent] {
    std::string error;
    if (const std::optional<std::string> body = GetBody(kApiUrl, user_agent, &error)) {
      check->release = gow3::setup::ParseRelease(*body);
      if (!check->release) error = "GitHub's answer could not be read.";
    }
    check->error = std::move(error);
    check->done.store(true);
  });
}

void StartUpdate(App& app, const fs::path& test_zip) {
  auto outcome = std::make_shared<Outcome>();
  u.outcome = outcome;
  u.prompt = Prompt::kWorking;
  const fs::path dir = app.dir;
  const Build build = app.state.build;
  const std::string tag = u.release.tag;
  g_update_job.Start([outcome, dir, build, tag, test_zip](const Progress& progress, std::string* error) {
    fs::path zip = test_zip;
    if (zip.empty()) {
      const fs::path tmp = dir / "update.tmp";
      auto fail = [&](ExtractResult r) {
        std::error_code ec;
        fs::remove_all(tmp, ec);
        return r;
      };
      // A failed rollback's originals are in update.tmp\backup: refuse rather than delete them.
      if (gow3::setup::UpdateBackupPending(dir)) return gow3::setup::Fail(error, kBackupPending);
      std::error_code ec;
      fs::remove_all(tmp, ec);
      fs::create_directories(tmp, ec);
      if (ec) return gow3::setup::Fail(error, "Could not create " + gow3::setup::Utf8(tmp) + ".");
      const std::string name = ZipName(tag);
      const std::wstring base = kDownloadBase + Wide(tag) + L"/";
      outcome->downloading = true;
      bool cancelled = false;
      for (const std::string& file : {name + ".sha256", name}) {
        const auto report = [&](uint64_t done, uint64_t total) {
          if (progress(done, total, "Downloading " + file)) return true;
          cancelled = true;
          return false;
        };
        if (!report(0, 0) || !HttpDownload(base + Wide(file), tmp / file, report, error)) {
          return fail(cancelled ? ExtractResult::kCancelled : ExtractResult::kFailed);
        }
      }
      outcome->downloading = false;
      zip = tmp / name;
    }
    const auto cancelled = [&] { return !progress(0, 0, "Installing"); };
    cancelled();
    return ApplyUpdate(dir, zip, build, error, cancelled) == UpdateResult::kOk ? ExtractResult::kOk
                                                                               : ExtractResult::kFailed;
  });
}

// The new launcher.exe, with the flags this one was started with (but not --update_zip).
bool StartNewLauncher(const App& app) {
  const fs::path exe = app.dir / "launcher.exe";
  std::wstring command = L"\"" + exe.wstring() + L"\"";
  if (app.start == App::Start::kSetup) command += L" --setup";
  if (app.start == App::Start::kOffer) command += L" --offer";
  if (app.start == App::Start::kRepair) command += L" --repair";
  if (!app.pass_args.empty()) command += L" -- " + app.pass_args;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, app.dir.c_str(), &si, &pi)) {
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

// On failure "Check now"'s status line says so (Game files shows it).
void SaveState(App& app) {
  if (!gow3::setup::SaveState(app.dir / "launcher.ini", app.state)) {
    u.status = "Could not write launcher.ini next to the launcher.";
  }
}

}  // namespace

void InitUpdate(App& app) {
  // Left by an update that finished (files the old launcher still had loaded); a failed
  // rollback's originals stay.
  gow3::setup::RemoveStaleUpdateTmp(app.dir);
  u.version = ReadVersionFile(app.dir / "version.txt");
  g_user_agent = L"gow3-launcher/" + (u.version.empty() ? std::wstring(L"dev") : Wide(u.version));
  if (!app.update_zip.empty()) {
    StartUpdate(app, app.update_zip);
    return;
  }
  if (u.version.empty() || !app.state.check_updates) return;
  if (static_cast<int64_t>(std::time(nullptr)) - app.state.last_check < kCheckInterval) return;
  StartCheck(false);
}

void CheckForUpdateNow(App&) {
  if (u.version.empty()) return;
  u.status = "Checking\xE2\x80\xA6";
  StartCheck(true);
}

bool UpdateChecksAvailable() { return !u.version.empty(); }
bool UpdateCheckRunning() { return u.check != nullptr; }
const std::string& UpdateStatus() { return u.status; }

void PollUpdate(App& app) {
  if (u.check && u.check->done.load()) {
    const std::shared_ptr<Check> check = std::move(u.check);
    u.thread.join();
    if (!check->release) {
      // Offline or rate-limited: only a "Check now" says so.
      if (u.manual) u.status = "Could not check for updates. " + check->error;
    } else {
      app.state.last_check = static_cast<int64_t>(std::time(nullptr));
      const ReleaseInfo& r = *check->release;
      const std::string zip = ZipName(r.tag);
      const auto has = [&](const std::string& n) {
        return std::find(r.asset_names.begin(), r.asset_names.end(), n) != r.asset_names.end();
      };
      const bool newer = !r.prerelease && gow3::setup::IsNewer(r.tag, u.version);
      const bool ready = has(zip) && has(zip + ".sha256");
      if (newer && ready && (u.manual || r.tag != app.state.skip_release)) {
        u.release = r;
        u.prompt = Prompt::kOffer;
        u.status.clear();
      } else if (u.manual) {
        u.status = newer ? "Version " + VersionOf(r.tag) + " is out, but its download isn't ready yet."
                         : "You have the latest version (" + u.version + ").";
      }
      SaveState(app);  // after the status, so a failure to save shows
    }
  }

  std::string error;
  if (const std::optional<ExtractResult> r = g_update_job.Finished(&error)) {
    u.outcome.reset();
    if (*r == ExtractResult::kOk) {
      if (StartNewLauncher(app)) {
        PostQuitMessage(0);
        u.prompt = Prompt::kNone;
        return;
      }
      error = "The update was installed, but the new launcher didn't start. Open launcher.exe again.";
    }
    if (*r == ExtractResult::kCancelled) {
      u.prompt = Prompt::kNone;
    } else {
      u.message = error;
      u.prompt = Prompt::kFailed;
    }
  }
}

void DrawUpdatePrompt(App& app) {
  using namespace theme;
  // Once open, the popup is submitted until it closes itself (also when PollUpdate ended it).
  const bool open = ImGui::IsPopupOpen("##update");
  if (u.prompt == Prompt::kNone && !open) return;
  if (!open) ImGui::OpenPopup("##update");
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(Dp(560.0f), Dp(380.0f)));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, kPanel);
  ImGui::PushStyleColor(ImGuiCol_Border, kOutline);
  ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.6f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Dp(24.0f), Dp(20.0f)));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Dp(8.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, Dp(1.0f));
  constexpr ImGuiWindowFlags kFlags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
  if (!ImGui::BeginPopupModal("##update", nullptr, kFlags)) {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
    return;
  }
  const float button_h = Dp(38.0f), gap = Dp(8.0f);
  const float bottom = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - button_h;
  // Buttons right-aligned along the bottom; returns the pressed label.
  struct Button {
    const char* label;
    bool primary = false;
    bool enabled = true;
  };
  auto buttons = [&](std::initializer_list<Button> list) {
    float total = 0.0f;
    for (const auto& [label, primary, enabled] : list) {
      total += std::max(ImGui::CalcTextSize(label).x + Dp(28.0f), primary ? Dp(120.0f) : 0.0f) + gap;
    }
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - total + gap, bottom));
    std::string pressed;
    bool first = true;
    for (const auto& [label, primary, enabled] : list) {
      if (!first) ImGui::SameLine(0, gap);
      first = false;
      ImGui::BeginDisabled(!enabled);
      const float w = std::max(ImGui::CalcTextSize(label).x + Dp(28.0f), primary ? Dp(120.0f) : 0.0f);
      if (primary) {
        ImGui::PushFont(HeadingFont(), 22.0f);
        if (PrimaryButton(label, ImVec2(w, button_h))) pressed = label;
        ImGui::PopFont();
      } else if (GhostButton(label, ImVec2(w, button_h))) {
        pressed = label;
      }
      ImGui::EndDisabled();
    }
    return pressed;
  };

  if (u.prompt == Prompt::kOffer) {
    const std::string title = "VERSION " + VersionOf(u.release.tag) + " IS AVAILABLE";
    PageTitle(title.c_str());
    ImGui::TextColored(kMuted, "You have %s. Your settings and game folder are kept.", u.version.c_str());
    ImGui::Dummy(ImVec2(0, Dp(4.0f)));
    const float box_h = bottom - ImGui::GetCursorPosY() - Dp(14.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Dp(6.0f));
    ImGui::BeginChild("##notes", ImVec2(0, box_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(u.release.body.empty() ? "No release notes." : u.release.body.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    const bool busy = GameFilesBusy();
    if (busy) {
      ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, bottom + (button_h - ImGui::GetFontSize()) * 0.5f));
      ImGui::TextColored(kMuted, "Wait for Game files to finish.");
    }
    const std::string b = buttons({{"Later"}, {"Skip this version"}, {"UPDATE", true, !busy}});
    if (b == "Later") {
      u.prompt = Prompt::kNone;
    } else if (b == "Skip this version") {
      app.state.skip_release = u.release.tag;
      SaveState(app);
      u.prompt = Prompt::kNone;
    } else if (b == "UPDATE") {
      StartUpdate(app, {});
    }
  } else if (u.prompt == Prompt::kWorking) {
    PageTitle("UPDATING");
    const uint64_t done = g_update_job.Done(), total = g_update_job.Total();
    ImGui::TextColored(kMuted, "%s", g_update_job.Current().c_str());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = Dp(6.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(kNavActive), h * 0.5f);
    if (total) {
      const float f = std::min(1.0f, static_cast<float>(double(done) / total));
      if (f > 0.0f) dl->AddRectFilled(p, ImVec2(p.x + w * f, p.y + h), ImGui::GetColorU32(kAccent), h * 0.5f);
    }
    ImGui::Dummy(ImVec2(w, h));
    if (total) ImGui::TextColored(kMuted, "%.1f of %.1f MB", done / 1048576.0, total / 1048576.0);
    const bool can_cancel = u.outcome && u.outcome->downloading.load() && !g_update_job.Cancelling();
    if (buttons({{"Cancel", false, can_cancel}}) == "Cancel") g_update_job.Cancel();
  } else if (u.prompt == Prompt::kFailed) {
    PageTitle("UPDATE FAILED", kAccent);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kText, "%s", u.message.c_str());
    ImGui::PopTextWrapPos();
    if (buttons({{"Close"}}) == "Close") u.prompt = Prompt::kNone;
  }
  if (u.prompt == Prompt::kNone) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(3);
}

void ShutdownUpdate() {
  // A check blocked on the network (up to 30 s per step) isn't waited for: it owns its result.
  if (u.thread.joinable()) {
    if (u.check && !u.check->done.load()) u.thread.detach();
    else u.thread.join();
  }
  g_update_job.Stop();
}

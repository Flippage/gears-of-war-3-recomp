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
bool Fetch(const std::wstring& url, const wchar_t* accept,
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
  session.h = WinHttpOpen(g_user_agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session.h) {  // before Windows 8.1
    session.h = WinHttpOpen(g_user_agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
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
  wchar_t final_url[4096];
  DWORD len = sizeof(final_url);
  if (!WinHttpQueryOption(request.h, WINHTTP_OPTION_URL, final_url, &len) ||
      std::wstring_view(final_url, len / sizeof(wchar_t)).substr(0, 8) != L"https://") {
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

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; });
  return s;
}

// Runs System32\tar.exe in `cwd` (never a PATH lookup); stdout to `out` when given. The exit code,
// or -1 when it couldn't run.
int RunTar(const std::wstring& args, const fs::path& cwd, const fs::path& out) {
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
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = nul;
  si.hStdOutput = file != INVALID_HANDLE_VALUE ? file : nul;
  si.hStdError = nul;
  PROCESS_INFORMATION pi{};
  int code = -1;
  if ((out.empty() || file != INVALID_HANDLE_VALUE) && nul != INVALID_HANDLE_VALUE &&
      CreateProcessW(tar.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si,
                     &pi)) {
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit = 0;
    if (GetExitCodeProcess(pi.hProcess, &exit)) code = static_cast<int>(exit);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
  return code;
}

// The files the update copies: regular files under src, relative, minus the kept ones; version.txt
// last. Links and anything SafeName rejects are skipped.
// The first line of a `tar -tf` listing that SafeName rejects (it would land outside update.tmp\x).
std::optional<std::string> UnsafeEntry(const fs::path& listing) {
  std::ifstream in(listing, std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && !gow3::setup::SafeName(line)) return line;
  }
  return std::nullopt;
}

std::vector<fs::path> FilesToCopy(const fs::path& src) {
  std::vector<fs::path> files;
  bool version = false;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(src, ec), end; !ec && it != end; it.increment(ec)) {
    if (!fs::is_regular_file(it->symlink_status(ec))) continue;
    const fs::path rel = it->path().lexically_relative(src);
    if (!gow3::setup::SafeName(gow3::setup::Utf8(rel))) continue;
    const std::string first = Lower(gow3::setup::Utf8(*rel.begin()));
    if (first == "update.tmp") continue;
    if (std::next(rel.begin()) == rel.end()) {
      if (first == "gow3.toml" || first == "gow3_game_path.txt" || first == "launcher.ini" ||
          first == "launcher.old.exe") {
        continue;
      }
      if (first == "version.txt") {
        version = true;
        continue;
      }
    }
    files.push_back(rel);
  }
  if (version) files.emplace_back("version.txt");
  return files;
}

}  // namespace

std::optional<std::string> HttpGet(const std::wstring& url, std::string* error) {
  std::string body;
  const bool ok = Fetch(url, L"application/vnd.github+json",
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

bool HttpDownload(const std::wstring& url, const fs::path& to, const std::function<bool(uint64_t, uint64_t)>& progress,
                  std::string* error) {
  std::ofstream out(to, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (error) *error = "Could not write " + gow3::setup::Utf8(to) + ".";
    return false;
  }
  uint64_t done = 0;
  const bool ok = Fetch(url, L"application/octet-stream",
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

UpdateResult ApplyUpdate(const fs::path& release_dir, const fs::path& zip, Build build, std::string* error) {
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
  std::error_code ec;
  fs::create_directories(tmp, ec);
  if (ec) return fail("Could not create " + Utf8(tmp) + ".");

  // <hash>  <zip name>: the first field must be the zip's SHA-256.
  {
    fs::path sha_file = zip;
    sha_file += L".sha256";
    std::string want;
    {
      std::ifstream in(sha_file, std::ios::binary);  // closed before any cleanup removes it
      in >> want;
    }
    if (want.empty()) return fail("Could not read " + Utf8(sha_file.filename()) + ".");
    const std::string have = Sha256File(zip);
    if (have.empty()) return fail("Could not read " + Utf8(zip.filename()) + ".");
    if (Lower(want) != have) {
      if (error) *error = "The download is damaged (its SHA-256 doesn't match). Nothing was changed.";
      cleanup();
      return UpdateResult::kHashMismatch;
    }
  }

  // tar runs inside update.tmp with relative paths where it can, so a non-ASCII release folder
  // never reaches its command line.
  const fs::path x = tmp / "x", list = tmp / "list.txt";
  fs::remove_all(x, ec);
  fs::create_directories(x, ec);
  if (ec) return fail("Could not create " + Utf8(x) + ".");
  const std::wstring zip_arg = L"\"" + (zip.parent_path() == tmp ? zip.filename() : zip).wstring() + L"\"";
  if (RunTar(L"-tf " + zip_arg, tmp, list) != 0) return fail("The update could not be read (tar.exe -t failed).");
  if (const std::optional<std::string> bad = UnsafeEntry(list)) {
    return fail("The update holds an unsafe path (" + *bad + "). Nothing was changed.");
  }
  if (RunTar(L"-xf " + zip_arg + L" -C x", tmp, {}) != 0) {
    return fail("The update could not be extracted (tar.exe failed).");
  }
  const fs::path src = x / zip.stem();  // gow3-recomp-<version>-win64
  if (!fs::is_regular_file(src / "launcher.exe", ec) || !fs::is_regular_file(src / "version.txt", ec)) {
    return fail("The update has no " + Utf8(zip.stem()) + "\\launcher.exe and version.txt.");
  }
  const std::vector<fs::path> files = FilesToCopy(src);

  for (int i = 0; gow3::setup::IsGameRunning(release_dir / "gow3.exe"); ++i) {
    if (i >= 120) {
      if (error) *error = "Close Gears of War 3 first, then try again.";
      cleanup();
      return UpdateResult::kGameRunning;
    }
    Sleep(250);
  }

  // The running launcher.exe can be renamed but not overwritten.
  const fs::path launcher = release_dir / "launcher.exe", old = release_dir / "launcher.old.exe";
  const bool had_launcher = fs::exists(launcher, ec);
  if (had_launcher && !MoveFileExW(launcher.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    return fail("Could not rename launcher.exe (error " + std::to_string(GetLastError()) + ").");
  }
  auto restore = [&](std::string msg) {
    if (had_launcher) MoveFileExW(old.c_str(), launcher.c_str(), MOVEFILE_REPLACE_EXISTING);
    return fail(std::move(msg) + " The launcher was put back; some files may be from the new version.");
  };
  for (const fs::path& rel : files) {
    const fs::path to = release_dir / rel;
    fs::create_directories(to.parent_path(), ec);
    if (ec || !fs::copy_file(src / rel, to, fs::copy_options::overwrite_existing, ec) || ec) {
      std::string why = ec ? ec.message() : std::string();
      while (!why.empty() && (why.back() == '.' || why.back() == ' ' || why.back() == '\r' || why.back() == '\n')) {
        why.pop_back();
      }
      return restore("Copying " + Utf8(rel) + " failed" + (why.empty() ? "" : " (" + why + ")") + ".");
    }
  }
  const gow3::setup::SwapResult swap = gow3::setup::SwapBuild(release_dir, build, std::chrono::seconds(30));
  if (swap == gow3::setup::SwapResult::kGameRunning) return restore("Gears of War 3 was started during the update.");
  if (swap != gow3::setup::SwapResult::kOk) {
    return restore(std::string("Could not switch gow3.exe to builds\\gow3-") +
                   (build == Build::kTu6 ? "tu6" : "base") + ".exe.");
  }
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
  u.thread = std::jthread([check = u.check] {
    std::string error;
    if (const std::optional<std::string> body = HttpGet(kApiUrl, &error)) {
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
    progress(0, 0, "Installing");
    return ApplyUpdate(dir, zip, build, error) == UpdateResult::kOk ? ExtractResult::kOk : ExtractResult::kFailed;
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

void SaveState(App& app) { gow3::setup::SaveState(app.dir / "launcher.ini", app.state); }

}  // namespace

void InitUpdate(App& app) {
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
      SaveState(app);
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
  auto buttons = [&](std::initializer_list<std::pair<const char*, bool>> list, bool enabled = true) {
    float total = 0.0f;
    for (const auto& [label, primary] : list) {
      total += std::max(ImGui::CalcTextSize(label).x + Dp(28.0f), primary ? Dp(120.0f) : 0.0f) + gap;
    }
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - total + gap, bottom));
    std::string pressed;
    bool first = true;
    ImGui::BeginDisabled(!enabled);
    for (const auto& [label, primary] : list) {
      if (!first) ImGui::SameLine(0, gap);
      first = false;
      const float w = std::max(ImGui::CalcTextSize(label).x + Dp(28.0f), primary ? Dp(120.0f) : 0.0f);
      if (primary) {
        ImGui::PushFont(HeadingFont(), 22.0f);
        if (PrimaryButton(label, ImVec2(w, button_h))) pressed = label;
        ImGui::PopFont();
      } else if (GhostButton(label, ImVec2(w, button_h))) {
        pressed = label;
      }
    }
    ImGui::EndDisabled();
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
    const std::string b = buttons({{"Later", false}, {"Skip this version", false}, {"UPDATE", true}});
    if (b == "Later") {
      u.prompt = Prompt::kNone;
    } else if (b == "Skip this version") {
      app.state.skip_release = u.release.tag;
      SaveState(app);
      u.prompt = Prompt::kNone;
    } else if (b == "UPDATE" && !busy) {
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
    if (buttons({{"Cancel", false}}, can_cancel) == "Cancel") g_update_job.Cancel();
  } else if (u.prompt == Prompt::kFailed) {
    PageTitle("UPDATE FAILED", kAccent);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kText, "%s", u.message.c_str());
    ImGui::PopTextWrapPos();
    if (buttons({{"Close", false}}) == "Close") u.prompt = Prompt::kNone;
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

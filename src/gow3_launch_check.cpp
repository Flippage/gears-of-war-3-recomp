// gow3 - ReXGlue Recompiled Project
//
// See gow3_launch_check.h. The decision itself is gow3::setup::DecideLaunch; this gathers its inputs
// and starts launcher.exe.

#include "gow3_launch_check.h"

#include <windows.h>

#include <string>
#include <system_error>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "gow3_title_update.h"
#include "setup/finder.h"
#include "setup/launch_decision.h"
#include "setup/state.h"
#include "setup/xex_info.h"

REXCVAR_DEFINE_BOOL(gow3_launcher_check, true, "Content",
                    "Hand over to launcher.exe when setup is needed (false for test runs)");

namespace rex::glue {

namespace {

// gow3.exe's own arguments, as typed: the command line after the program name.
std::wstring OriginalArguments() {
  const wchar_t* p = GetCommandLineW();
  if (*p == L'"') {
    ++p;
    while (*p && *p != L'"') ++p;
    if (*p) ++p;
  } else {
    while (*p && *p != L' ' && *p != L'\t') ++p;
  }
  while (*p == L' ' || *p == L'\t') ++p;
  return p;
}

}  // namespace

void RunLaunchCheck(const std::filesystem::path& game_data_root) {
  using namespace gow3::setup;
  if (!REXCVAR_GET(gow3_launcher_check)) return;
  const std::filesystem::path exe_dir = rex::filesystem::GetExecutablePath().parent_path();
  const std::filesystem::path launcher = exe_dir / "launcher.exe";
  std::error_code ec;
  if (!std::filesystem::is_regular_file(launcher, ec)) return;

#ifdef GOW3_TU6
  const Build build = Build::kTu6;
#else
  const Build build = Build::kBase;
#endif
  const LauncherState state = LoadState(exe_dir / "launcher.ini");
  const bool game_valid = IsGameFolder(game_data_root);
  std::vector<FoundPackage> packages;
  if (game_valid) {
    const auto xex = ReadXexInfoFile(game_data_root / "default.xex");
    packages = FindPackages(PackageRoots(exe_dir, game_data_root), xex ? xex->media_id : 0);
  }
  const LaunchInputs in{build, game_valid, FindTitleUpdateFolder(game_data_root).has_value(), &packages,
                        &state};
  const char* flag = nullptr;
  switch (DecideLaunch(in)) {
    case LaunchAction::kStartGame: return;
    case LaunchAction::kSetup: flag = "--setup"; break;
    case LaunchAction::kOffer: flag = "--offer"; break;
    case LaunchAction::kRepair: flag = "--repair"; break;
  }

  std::wstring command = L"\"" + launcher.wstring() + L"\" ";
  command += std::wstring(flag, flag + std::char_traits<char>::length(flag)) + L" --";
  if (const std::wstring args = OriginalArguments(); !args.empty()) command += L" " + args;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  const std::wstring dir = exe_dir.wstring();
  if (!CreateProcessW(launcher.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(),
                      &si, &pi)) {
    REXLOG_ERROR("Launcher: starting launcher.exe failed ({}); starting the game", GetLastError());
    return;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  REXLOG_INFO("Launcher: handed over to launcher.exe {}", flag);
  rex::FlushLogging();
  ExitProcess(0);
}

}  // namespace rex::glue

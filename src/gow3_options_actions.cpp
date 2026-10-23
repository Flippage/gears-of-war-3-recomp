// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.

#include "gow3_options_actions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <system_error>

#include <rex/filesystem.h>
#include <rex/logging.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_4.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#endif

namespace rex::glue::options_actions {
namespace {

#ifdef _WIN32
std::string WideToUtf8(const wchar_t* w) {
  if (!w || !*w) {
    return {};
  }
  int needed = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (needed <= 1) {
    return {};
  }
  std::string out(static_cast<size_t>(needed - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), needed, nullptr, nullptr);
  return out;
}

BOOL CALLBACK CollectMonitorProc(HMONITOR hmon, HDC, LPRECT, LPARAM data) {
  auto* out = reinterpret_cast<std::vector<MonitorInfo>*>(data);
  MONITORINFOEXW mi{};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(hmon, &mi)) {
    return TRUE;
  }
  int idx = static_cast<int>(out->size());
  int w = static_cast<int>(mi.rcMonitor.right - mi.rcMonitor.left);
  int h = static_cast<int>(mi.rcMonitor.bottom - mi.rcMonitor.top);
  bool primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

  // Prefer the real physical mode from EnumDisplaySettingsW over the DPI-virtualized rect.
  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmPelsWidth > 0 &&
      dm.dmPelsHeight > 0) {
    w = static_cast<int>(dm.dmPelsWidth);
    h = static_cast<int>(dm.dmPelsHeight);
  }

  std::string friendly;
  DISPLAY_DEVICEW dd{};
  dd.cb = sizeof(dd);
  if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0) && dd.DeviceString[0]) {
    friendly = WideToUtf8(dd.DeviceString);
  }
  if (friendly.empty()) {
    friendly = WideToUtf8(mi.szDevice);
  }

  char buf[256];
  std::snprintf(buf, sizeof(buf), "%d: %s (%dx%d%s)", idx + 1, friendly.c_str(), w, h,
                primary ? ", Primary" : "");

  MonitorInfo info;
  info.index = idx;
  info.label = buf;
  info.width = w;
  info.height = h;
  info.primary = primary;
  out->push_back(std::move(info));
  return TRUE;
}
#endif

std::string NarrowPath(const std::filesystem::path& p) {
#ifdef _WIN32
  return WideToUtf8(p.c_str());
#else
  return p.string();
#endif
}

bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b) {
  if (a.empty() || b.empty()) {
    return false;
  }
  std::error_code ec1;
  std::error_code ec2;
  auto ca = std::filesystem::weakly_canonical(a, ec1);
  auto cb = std::filesystem::weakly_canonical(b, ec2);
  if (!ec1 && !ec2) {
    return ca == cb;
  }
  return a.lexically_normal() == b.lexically_normal();
}

bool HasSaveData(const std::filesystem::path& dir) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    return false;
  }
  // Gears of War 3 title ID 4D5308AB.
  if (std::filesystem::is_directory(dir / "4D5308AB", ec) ||
      std::filesystem::is_directory(dir / "content" / "4D5308AB", ec)) {
    return true;
  }
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    auto name = entry.path().filename().string();
    if (name == "cache") {
      continue;
    }
    return true;
  }
  return false;
}

}  // namespace

std::vector<MonitorInfo> ListMonitors() {
  std::vector<MonitorInfo> out;
#ifdef _WIN32
  EnumDisplayMonitors(nullptr, nullptr, &CollectMonitorProc, reinterpret_cast<LPARAM>(&out));
#endif
  if (out.empty()) {
    out.push_back({0, "1: Primary Monitor", 0, 0, true});
  }
  return out;
}

std::vector<GpuInfo> ListGpus() {
  std::vector<GpuInfo> out;
#ifdef _WIN32
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return out;
  }
  for (UINT i = 0;; ++i) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if ( factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc))) {
      continue;
    }
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      continue;
    }
    std::string name = WideToUtf8(desc.Description);
    if (name.empty()) {
      name = "GPU " + std::to_string(i);
    }
    const double gb =
        static_cast<double>(desc.DedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0);
    char label[256];
    if (gb >= 0.5) {
      std::snprintf(label, sizeof(label), "%s (%.0f GB)", name.c_str(), std::round(gb));
    } else {
      std::snprintf(label, sizeof(label), "%s", name.c_str());
    }
    out.push_back({static_cast<int>(i), label});
  }
#endif
  return out;
}

std::filesystem::path DefaultSaveDir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH];
  const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    return std::filesystem::path(buf) / L"GearsOfWar3PC" / L"user";
  }
#endif
  return rex::filesystem::GetUserFolder() / "gow3";
}

std::vector<SaveFolderChoice> ListSaveFolders(const std::filesystem::path& current_dir) {
  std::vector<SaveFolderChoice> out;
  const std::filesystem::path default_dir = DefaultSaveDir();
  const std::filesystem::path exe_dir = rex::filesystem::GetExecutableFolder();
  const std::filesystem::path portable_dir = exe_dir / "user";

  auto make_label = [](const std::string& prefix, const std::filesystem::path& p, bool saves) {
    std::string s = prefix + " (" + NarrowPath(p) + ")";
    if (saves) {
      s += "  [saves found]";
    }
    return s;
  };

  const bool def_saves = HasSaveData(default_dir);
  out.push_back({"", make_label("Default", default_dir, def_saves), default_dir, def_saves});

  if (!SamePath(portable_dir, default_dir)) {
    const bool port_saves = HasSaveData(portable_dir);
    out.push_back({NarrowPath(portable_dir), make_label("Game folder", portable_dir, port_saves),
                   portable_dir, port_saves});
  }

  // If the player has selected a custom folder (via Browse or gow3.toml), make
  // sure it appears in the choice list as well.
  if (!current_dir.empty() && !SamePath(current_dir, default_dir) &&
      !SamePath(current_dir, portable_dir)) {
    const bool cur_saves = HasSaveData(current_dir);
    out.push_back({NarrowPath(current_dir), make_label("Custom", current_dir, cur_saves),
                   current_dir, cur_saves});
  }

  return out;
}

std::string PickFolder(const std::filesystem::path& current_dir) {
#ifdef _WIN32
  HRESULT hr_init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  const bool should_uninit = SUCCEEDED(hr_init);

  std::string picked;
  IFileOpenDialog* dlg = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg));
  if (SUCCEEDED(hr) && dlg) {
    DWORD opts = 0;
    if (SUCCEEDED(dlg->GetOptions(&opts))) {
      dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    dlg->SetTitle(L"Select Gears of War 3 Save Folder");
    if (!current_dir.empty()) {
      IShellItem* start_item = nullptr;
      if (SUCCEEDED(SHCreateItemFromParsingName(current_dir.c_str(), nullptr,
                                                IID_PPV_ARGS(&start_item))) &&
          start_item) {
        dlg->SetFolder(start_item);
        start_item->Release();
      }
    }
    if (SUCCEEDED(dlg->Show(GetActiveWindow()))) {
      IShellItem* item = nullptr;
      if (SUCCEEDED(dlg->GetResult(&item)) && item) {
        PWSTR psz = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
          picked = WideToUtf8(psz);
          CoTaskMemFree(psz);
        }
        item->Release();
      }
    }
    dlg->Release();
  } else {
    BROWSEINFOW bi{};
    bi.hwndOwner = GetActiveWindow();
    bi.lpszTitle = L"Select Gears of War 3 Save Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
      wchar_t path_buf[MAX_PATH]{};
      if (SHGetPathFromIDListW(pidl, path_buf)) {
        picked = WideToUtf8(path_buf);
      }
      CoTaskMemFree(pidl);
    }
  }

  if (should_uninit) {
    CoUninitialize();
  }
  return picked;
#else
  (void)current_dir;
  return {};
#endif
}

bool OpenFolder(const std::filesystem::path& dir) {
  if (dir.empty()) {
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
#ifdef _WIN32
  HINSTANCE res =
      ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<uintptr_t>(res) > 32;
#else
  return false;
#endif
}

void WaitForPreviousInstance() {
#ifdef _WIN32
  const wchar_t* cmd = GetCommandLineW();
  if (!cmd) {
    return;
  }
  const wchar_t* tag = std::wcsstr(cmd, L"--gow3_wait_pid=");
  if (!tag) {
    return;
  }
  tag += std::wcslen(L"--gow3_wait_pid=");
  DWORD pid = static_cast<DWORD>(std::wcstoul(tag, nullptr, 10));
  if (pid == 0 || pid == GetCurrentProcessId()) {
    return;
  }
  HANDLE proc = OpenProcess(SYNCHRONIZE, FALSE, pid);
  if (proc) {
    WaitForSingleObject(proc, 10000);
    CloseHandle(proc);
  }
  Sleep(150);
#endif
}

bool RestartGame() {
#ifdef _WIN32
  wchar_t exe_path[MAX_PATH]{};
  DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return false;
  }

  std::wstring cmd_line;
  if (const wchar_t* orig = GetCommandLineW(); orig && *orig) {
    cmd_line = orig;
    const std::wstring needle = L" --gow3_wait_pid=";
    auto pos = cmd_line.find(needle);
    if (pos != std::wstring::npos) {
      auto end = cmd_line.find(L' ', pos + needle.size());
      cmd_line.erase(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
    }
    cmd_line += L" --gow3_wait_pid=" + std::to_wstring(GetCurrentProcessId());
  } else {
    cmd_line = L"\"" + std::wstring(exe_path) + L"\" --gow3_wait_pid=" +
               std::to_wstring(GetCurrentProcessId());
  }

  std::wstring work_dir = std::filesystem::path(exe_path).parent_path().wstring();

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(exe_path, cmd_line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                           work_dir.empty() ? nullptr : work_dir.c_str(), &si, &pi);
  if (!ok) {
    REXLOG_ERROR("RestartGame: CreateProcessW failed ({})", GetLastError());
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  REXLOG_INFO("Restarting Gears of War 3 (new process launched)...");
  rex::FlushLogging();
  TerminateProcess(GetCurrentProcess(), 0);
  return true;
#else
  return false;
#endif
}

}  // namespace rex::glue::options_actions

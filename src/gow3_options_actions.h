// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.
//
// Host OS actions invoked by the PC Options overlay: monitor and DXGI GPU
// enumeration, save folder discovery/picker/Explorer launch, and clean
// single-instance game restart.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rex::glue::options_actions {

struct MonitorInfo {
  int index = 0;        // Matches the index expected by the `monitor` cvar.
  std::string label;    // Human-readable ("1: DELL U2720Q (3840x2160, Primary)").
  int width = 0;
  int height = 0;
  bool primary = false;
};

std::vector<MonitorInfo> ListMonitors();

struct GpuInfo {
  int index = 0;        // Matches the DXGI adapter index expected by `d3d12_adapter`.
  std::string label;    // Human-readable ("NVIDIA GeForce RTX 4080 (16 GB)").
};

std::vector<GpuInfo> ListGpus();

struct SaveFolderChoice {
  std::string value;           // Stored in `user_data_root` ("" for default, or absolute path).
  std::string label;           // Shown in the Save Folder selector.
  std::filesystem::path path;  // Resolved directory on disk.
  bool has_saves = false;      // True when 4D5308AB or user save files exist inside `path`.
};

std::filesystem::path DefaultSaveDir();
std::vector<SaveFolderChoice> ListSaveFolders(const std::filesystem::path& current_dir);

// Opens the native folder-picker dialog and returns the selected path as UTF-8
// (or an empty string if the user cancelled).
std::string PickFolder(const std::filesystem::path& current_dir);

// Opens `dir` in the system file manager (creating it first if needed).
bool OpenFolder(const std::filesystem::path& dir);

// Relaunches the current executable with the same command-line arguments plus
// `--gow3_wait_pid=<pid>` and terminates the current process.
bool RestartGame();

// If `--gow3_wait_pid=<pid>` is present on the command line, waits for that
// process to exit before continuing startup.
void WaitForPreviousInstance();

}  // namespace rex::glue::options_actions

#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>

#include "setup/finder.h"
#include "setup/state.h"
#include "setup/stfs.h"

namespace gow3::setup {

enum class SwapResult { kOk, kGameRunning, kFailed };
enum class WaitResult { kExited, kTimedOut, kCancelled };

// Waits up to `wait` (polling every 250 ms) for gow3.exe to exit, then replaces
// <release_dir>\gow3.exe with builds\gow3-<base|tu6>.exe via gow3.exe.new (made writable, as the
// source may be read-only). gow3.exe is left untouched on any failure.
SwapResult SwapBuild(const std::filesystem::path& release_dir, Build build, std::chrono::seconds wait);
// A process whose image is this exact file (case-insensitive).
bool IsGameRunning(const std::filesystem::path& gow3_exe);
// Polls every 250 ms, for up to `wait`, until no process runs gow3_exe. `cancelled` is asked on
// each poll while it still runs; true stops the wait.
WaitResult WaitForGameExit(const std::filesystem::path& gow3_exe, std::chrono::milliseconds wait,
                           const std::function<bool()>& cancelled = {});
// Same size and the same bytes (false when either can't be read).
bool SameFileContents(const std::filesystem::path& a, const std::filesystem::path& b);
// Extracts the TU to <game>\TU6.part-dir, then swaps it in as <game>\TU6. A failure leaves an
// existing TU6 as it was.
ExtractResult InstallTitleUpdate(const FoundPackage& tu, const std::filesystem::path& game,
                                 const Progress& progress, std::string* error);
// Copies the package to <game>\DLC\<file name>, overwriting; the source is untouched.
bool InstallDlc(const FoundPackage& dlc, const std::filesystem::path& game, std::string* error);

}  // namespace gow3::setup

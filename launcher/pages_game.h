#pragma once
// The Play page and the Game files pages (set up, extracting, new content, repair, overview).
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "setup/stfs.h"

struct App;

// One background task at a time (an extraction, an install, a build switch). The worker reports
// through the Progress it is handed, which also returns false once Cancel() was called. The UI
// thread only reads the atomics and the mutex-guarded strings.
class Job {
 public:
  using Work = std::function<gow3::setup::ExtractResult(const gow3::setup::Progress&, std::string*)>;
  void Start(Work work);  // only when !Running() and the last result was collected
  bool Running() const { return running_.load(); }
  void Cancel() {
    cancelled_ = true;
    thread_.request_stop();
  }
  bool Cancelling() const { return cancelled_.load(); }
  uint64_t Done() const { return done_.load(); }
  uint64_t Total() const { return total_.load(); }
  std::string Current() const;
  // The result once, after the worker returned (it is joined then); nullopt while it runs or idle.
  std::optional<gow3::setup::ExtractResult> Finished(std::string* error);
  // Cancel and join: before the launcher exits.
  void Stop();

 private:
  std::jthread thread_;
  std::atomic<bool> running_{false}, finished_{false}, cancelled_{false};
  std::atomic<uint64_t> done_{0}, total_{0};
  mutable std::mutex mutex_;
  std::string current_, error_;
  gow3::setup::ExtractResult result_ = gow3::setup::ExtractResult::kOk;
};

// IFileOpenDialog with FOS_PICKFOLDERS. Empty when cancelled.
std::filesystem::path PickFolder(HWND owner, const std::filesystem::path& start);
// IFileOpenDialog filtered to *.iso, with a "Use this folder" button that returns the folder being
// shown instead. Empty when cancelled.
std::filesystem::path PickIsoOrFolder(HWND owner);

// Re-reads what the Play page shows: the build's content (<game>\DLC) and the renderer (gow3.toml).
// Call at start and whenever the game folder, build or content changes.
void RefreshPlayPage(const App& app);
void DrawPlayPage(App& app);

// Picks the first Game files view from App::start and what is on disk (finds the game or an ISO
// when no game is set). Call once, after RefreshPlayPage.
void InitGameFiles(App& app);
// Collects a finished job and moves the flow on. Call every frame, whatever page is shown.
void PollGameFiles(App& app);
void DrawGameFilesPage(App& app);
// An extraction, install or build switch is running (PLAY waits for it).
bool GameFilesBusy();
// Cancels a running job and waits for it.
void ShutdownGameFiles();

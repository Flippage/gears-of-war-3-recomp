#pragma once
// Release update: the once-a-day GitHub check, the prompt, and download, verify and install.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "setup/state.h"

struct App;

// HTTPS GET through WinHTTP, user agent "gow3-launcher/<version>", following redirects (never to a
// non-https URL), 30 s timeouts. The body on HTTP 200; otherwise nullopt and a message for the player.
std::optional<std::string> HttpGet(const std::wstring& url, std::string* error);
// The same, streamed to `to`. progress(done, total) returns false to cancel (total 0 when unknown).
bool HttpDownload(const std::wstring& url, const std::filesystem::path& to,
                  const std::function<bool(uint64_t, uint64_t)>& progress, std::string* error);
// Lower-case hex SHA-256 of the file (BCrypt); empty when it can't be read.
std::string Sha256File(const std::filesystem::path&);

enum class UpdateResult { kOk, kHashMismatch, kGameRunning, kFailed };
// Installs <zip> (gow3-recomp-<v>-win64.zip, with <zip>.sha256 next to it) over release_dir:
// checks the hash, lists the zip (every entry must pass SafeName), extracts it into
// <release_dir>\update.tmp\x with System32\tar.exe, waits up to 30 s for gow3.exe to close,
// renames launcher.exe to launcher.old.exe, copies the files over the release (never gow3.toml,
// gow3_game_path.txt or launcher.ini; version.txt last), then SwapBuild(release_dir, build) and
// removes update.tmp. A failure after the rename puts launcher.exe back. Deletes nothing outside
// update.tmp.
UpdateResult ApplyUpdate(const std::filesystem::path& release_dir, const std::filesystem::path& zip,
                         gow3::setup::Build build, std::string* error);

// Reads version.txt; with --update_zip installs that zip straight away (test only, no HTTP),
// otherwise starts the daily check when it is due. Call once, after InitGameFiles.
void InitUpdate(App& app);
// "Check now" on Game files: checks whatever last_check says and shows a skipped version too.
void CheckForUpdateNow(App& app);
// version.txt was found (a release, not a dev build).
bool UpdateChecksAvailable();
bool UpdateCheckRunning();
// The result of the last "Check now", for the Game files page; empty when there is none.
const std::string& UpdateStatus();
// Collects the check and the update job. Call every frame.
void PollUpdate(App& app);
// The modal prompt and the update's progress, over everything else. Call after the frame's pages.
void DrawUpdatePrompt(App& app);
// Before the launcher exits: lets a running check go and joins the update job.
void ShutdownUpdate();

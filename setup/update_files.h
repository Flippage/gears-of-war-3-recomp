#pragma once
// The file-level rules of a release update (the launcher's ApplyUpdate does the I/O).
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gow3::setup {

// "<hash>  <zip name>" (package.ps1): the first field, lower-cased, when it is 64 hex digits.
std::optional<std::string> ParseSha256File(std::string_view text);

// The first line of a `tar -tf` listing that SafeName rejects (it would land outside the
// extraction folder); nullopt when every entry is safe.
std::optional<std::string> UnsafeEntry(std::string_view listing);

// Regular files under src (the zip's gow3-recomp-<v>-win64 folder), relative, that the update
// copies over the release. Left out: links, names SafeName rejects, anything under update.tmp, and
// at the top level gow3.toml, gow3_game_path.txt, launcher.ini (kept), gow3.exe (SwapBuild writes
// it), launcher.old.exe and version.txt (copied by ApplyUpdate after the swap). Names compare
// case-insensitively.
std::vector<std::filesystem::path> FilesToCopy(const std::filesystem::path& src);

// update.tmp\rollback-failed: written when an update's rollback could not put every file back (the
// originals are then in update.tmp\backup). update.tmp is never removed while it exists.
inline constexpr const wchar_t* kRollbackFailedMarker = L"rollback-failed";
// <release_dir>\update.tmp holds originals a failed rollback left behind (kRollbackFailedMarker).
// Files in update.tmp\backup alone mean nothing: after a good update the replaced DLLs the
// launcher had loaded stay there until it exits.
bool UpdateBackupPending(const std::filesystem::path& release_dir);
// Removes a leftover <release_dir>\update.tmp (a finished update could not delete files that were
// still loaded) unless UpdateBackupPending. Errors are ignored; the next start tries again.
void RemoveStaleUpdateTmp(const std::filesystem::path& release_dir);

}  // namespace gow3::setup

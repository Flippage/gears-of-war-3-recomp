#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "setup/stfs.h"

namespace gow3::setup {

struct DiscEntry {
  std::string path;  // UTF-8, '\' separated
  uint64_t offset;   // absolute byte offset in the image
  uint64_t size;
  bool dir;
};

// Read-only view of an Xbox 360 disc image (XDVDFS). Holds the path and the entry list; the
// image is re-opened for each read.
class DiscImage {
 public:
  // Probes the known partition offsets; nullopt (and *error) when none holds a valid volume.
  static std::optional<DiscImage> Open(const std::filesystem::path& iso, std::string* error);

  const std::vector<DiscEntry>& Entries() const { return entries_; }
  uint64_t TotalBytes() const { return total_; }  // sum of file sizes

  // First max_bytes of a file (case-insensitive path); nullopt when missing or unreadable.
  std::optional<std::vector<uint8_t>> ReadFile(std::string_view path, size_t max_bytes) const;

  // Extracts every file under out_dir. A file in progress is written to <name>.part and
  // renamed when complete; it is deleted on cancel or failure. Existing files are overwritten.
  ExtractResult ExtractAll(const std::filesystem::path& out_dir, const Progress& progress,
                           std::string* error) const;

 private:
  DiscImage() = default;
  std::filesystem::path path_;
  std::vector<DiscEntry> entries_;
  uint64_t total_ = 0;
};

}  // namespace gow3::setup

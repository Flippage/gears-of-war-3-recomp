#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace gow3::setup {

struct XexInfo {
  uint32_t title_id = 0;
  uint32_t media_id = 0;
  uint32_t version = 0;
};

// `header` is the first <= 64 KiB of the file. nullopt when it is not a XEX2
// or has no execution-info header.
std::optional<XexInfo> ReadXexInfo(std::span<const uint8_t> header);
std::optional<XexInfo> ReadXexInfoFile(const std::filesystem::path& xex);
// Eight uppercase hex digits, e.g. "4D5308AB".
std::string Hex8(uint32_t v);

}  // namespace gow3::setup
